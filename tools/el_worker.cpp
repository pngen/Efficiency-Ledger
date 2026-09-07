#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#include "efficiency_ledger/codec.hpp"
#include "efficiency_ledger/frame_io.hpp"
#include "efficiency_ledger/socket.hpp"

using namespace el;

namespace {

LedgerEntry mk(std::uint64_t id, EventType ev, EfficiencyClass cls, std::uint64_t ns,
               AccountId acc, std::uint64_t pub, WorkerBootId boot, AttemptId att,
               AttemptOutcome out) {
  LedgerEntry e;
  e.id = AccountingEntryId(id);
  e.event = ev;
  e.classification = cls;
  e.usage.set(ResourceDimension::kAcceleratorNanoseconds, ns);
  e.account = acc;
  e.fences.publication = PublicationId(pub);
  e.fences.worker_boot = boot;
  e.attempt.id = att;
  e.attempt_outcome = out;
  if (cls == EfficiencyClass::kUseful) e.subcategory.useful = UsefulReason::kAuthoritativeExecution;
  if (cls == EfficiencyClass::kAvoidableWaste) e.subcategory.waste = WasteReason::kFailedAttempt;
  return e;
}

std::string u64_body(std::uint64_t v) {
  std::string s;
  for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
  return s;
}

bool send_append(Socket& conn, const LedgerEntry& e) {
  std::string body;
  encode_entry_bytes(e, body);
  if (!write_frame(conn, static_cast<std::uint32_t>(MessageKind::kAppend), 0, body)) return false;
  Status st;
  std::string payload;
  if (!read_status(conn, st, payload)) return false;
  std::printf("append id=%llu -> %s\n", static_cast<unsigned long long>(e.id.value()),
              st.ok() ? "OK" : st.message().c_str());
  return st.ok();
}

}  // namespace

int main(int argc, char** argv) {
  std::uint16_t port = 37123;
  std::uint64_t boot = 100;
  std::string mode = "append";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    else if (a == "--boot" && i + 1 < argc) boot = std::stoull(argv[++i]);
    else if (a == "--mode" && i + 1 < argc) mode = argv[++i];
  }

#ifdef _WIN32
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::fprintf(stderr, "WSAStartup failed\n"); return 1; }
#endif

  Socket conn;
  if (!conn.connect_to("127.0.0.1", port)) { std::fprintf(stderr, "connect failed\n"); return 1; }

  if (!write_frame(conn, static_cast<std::uint32_t>(MessageKind::kRegister), 0, u64_body(boot))) {
    return 1;
  }
  Status st;
  std::string payload;
  if (!read_status(conn, st, payload)) return 1;
  if (!st.ok()) { std::fprintf(stderr, "register failed\n"); return 1; }

  const AccountId acc(3);
  const WorkerBootId wb(boot);

  if (mode == "scenario") {
    // attempt 1 fails (300 ns), attempt 2 succeeds + publishes (200 ns),
    // stale same-attempt completion rejected, duplicate id rejected.
    send_append(conn, mk(1, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 300,
                         acc, 0, wb, AttemptId(11), AttemptOutcome::kFailed));
    send_append(conn, mk(2, EventType::kOutputPublished, EfficiencyClass::kUseful, 200,
                         acc, 1, wb, AttemptId(12), AttemptOutcome::kSucceeded));
    send_append(conn, mk(3, EventType::kExecutionCompleted, EfficiencyClass::kUseful, 200,
                         acc, 1, wb, AttemptId(12), AttemptOutcome::kSucceeded));  // stale terminal
    send_append(conn, mk(1, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 300,
                         acc, 0, wb, AttemptId(11), AttemptOutcome::kFailed));  // duplicate id
  } else if (mode == "append") {
    send_append(conn, mk(10, EventType::kOutputPublished, EfficiencyClass::kUseful, 250,
                         acc, 5, wb, AttemptId(20), AttemptOutcome::kSucceeded));
  } else if (mode == "killsleep") {
    send_append(conn, mk(20, EventType::kOutputPublished, EfficiencyClass::kUseful, 150,
                         acc, 6, wb, AttemptId(21), AttemptOutcome::kSucceeded));
#ifdef _WIN32
    Sleep(60000);  // stay alive so the test can terminate this real process
#else
    sleep(60);
#endif
  }

  conn.close();
#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
