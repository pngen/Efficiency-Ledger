#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "efficiency_ledger/codec.hpp"
#include "efficiency_ledger/frame_io.hpp"
#include "efficiency_ledger/socket.hpp"

#include "testutil.hpp"

using namespace el;

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <windows.h>
#else
#define EL_DIST_NOWIN
#endif

namespace {

std::string exe_dir() {
#ifdef _WIN32
  char buf[MAX_PATH];
  GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string p(buf);
  auto slash = p.find_last_of("\\/");
  if (slash != std::string::npos) p = p.substr(0, slash);
  return p;
#else
  return "";
#endif
}

std::string join(const std::string& a, const std::string& b) { return a + "\\" + b; }

// Spawn a child process; returns true if created.
bool spawn(const std::string& file, const std::string& args, void* out_handle) {
#ifdef _WIN32
  std::string cmdline = "\"" + file + "\" " + args;
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::string copy = cmdline;
  BOOL okc = CreateProcessA(file.c_str(), &copy[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                            &si, &pi);
  if (!okc) return false;
  if (out_handle) *static_cast<HANDLE*>(out_handle) = pi.hProcess;
  CloseHandle(pi.hThread);
  return true;
#else
  (void)file; (void)args; (void)out_handle;
  return false;
#endif
}

void terminate(void* handle) {
#ifdef _WIN32
  if (handle) TerminateProcess(static_cast<HANDLE*>(handle), 0);
#endif
}

void wait_ms(int ms) {
#ifdef _WIN32
  Sleep(static_cast<DWORD>(ms));
#endif
}

bool connect_retry(Socket& s, std::uint16_t port) {
  for (int i = 0; i < 200; ++i) {
    Socket tmp;
    if (tmp.connect_to("127.0.0.1", port)) { s = std::move(tmp); return true; }
    wait_ms(25);
  }
  return false;
}

bool send_register(Socket& s, std::uint64_t boot) {
  std::string body;
  for (int i = 0; i < 8; ++i) body.push_back(static_cast<char>((boot >> (8 * i)) & 0xFFu));
  if (!write_frame(s, static_cast<std::uint32_t>(MessageKind::kRegister), 0, body)) return false;
  Status st; std::string payload;
  return read_status(s, st, payload) && st.ok();
}

LedgerEntry mk_append(std::uint64_t id, std::uint64_t ns, std::uint64_t pub, std::uint64_t boot,
                      std::uint64_t attempt_id, EfficiencyClass cls, EventType ev,
                      AttemptOutcome out) {
  LedgerEntry e;
  e.id = AccountingEntryId(id);
  e.event = ev;
  e.classification = cls;
  e.usage.set(ResourceDimension::kAcceleratorNanoseconds, ns);
  e.account = AccountId(3);
  e.fences.publication = PublicationId(pub);
  e.fences.worker_boot = WorkerBootId(boot);
  e.attempt.id = AttemptId(attempt_id);
  e.attempt_outcome = out;
  if (cls == EfficiencyClass::kUseful) e.subcategory.useful = UsefulReason::kAuthoritativeExecution;
  if (cls == EfficiencyClass::kAvoidableWaste) e.subcategory.waste = WasteReason::kFailedAttempt;
  return e;
}

bool send_append(Socket& s, const LedgerEntry& e) {
  std::string body;
  encode_entry_bytes(e, body);
  if (!write_frame(s, static_cast<std::uint32_t>(MessageKind::kAppend), 0, body)) return false;
  Status st; std::string payload;
  if (!read_status(s, st, payload)) return false;
  return st.ok();
}

bool query_digest(Socket& s, std::uint64_t& digest, std::uint64_t& count) {
  if (!write_frame(s, static_cast<std::uint32_t>(MessageKind::kQueryDigest), 0, "")) return false;
  Status st; std::string payload;
  if (!read_status(s, st, payload) || !st.ok()) return false;
  if (payload.size() < 16) return false;
  digest = 0; count = 0;
  for (int i = 0; i < 8; ++i) digest |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(payload[i])) << (8 * i);
  for (int i = 0; i < 8; ++i) count |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(payload[8 + i])) << (8 * i);
  return true;
}

void run() {
  std::fprintf(stderr, "[dist] entering run()\n");
  const std::string dir = exe_dir();
  std::fprintf(stderr, "[dist] dir=%s\n", dir.c_str());
  const std::string coord = join(dir, "el_coordinator.exe");
  const std::string worker = join(dir, "el_worker.exe");
  std::string state = dir + "\\dist_state.led";

  std::uint16_t port = static_cast<std::uint16_t>(40000 + (GetTickCount() % 3000));

  std::remove(state.c_str());

  void* coord_h = nullptr;
  std::string cargs = "--port " + std::to_string(port) + " --path \"" + state + "\"";
  CHECK(spawn(coord, cargs, &coord_h));
  std::fprintf(stderr, "[dist] spawned coord\n");

  Socket cli;
  bool cc = connect_retry(cli, port);
  std::fprintf(stderr, "[dist] connect_retry=%d\n", cc ? 1 : 0);
  CHECK(cc);

  bool reg = send_register(cli, 100);
  std::fprintf(stderr, "[dist] reg=%d\n", reg ? 1 : 0);
  CHECK(reg);
  std::fprintf(stderr, "[dist] before a1\n");
  bool a1 = send_append(cli, mk_append(1, 300, 0, 100, 11, EfficiencyClass::kAvoidableWaste,
                                   EventType::kAttemptFailed, AttemptOutcome::kFailed));
  std::fprintf(stderr, "[dist] a1=%d\n", a1 ? 1 : 0);
  CHECK(a1);
  bool a2 = send_append(cli, mk_append(2, 200, 1, 100, 12, EfficiencyClass::kUseful,
                                   EventType::kOutputPublished, AttemptOutcome::kSucceeded));
  std::fprintf(stderr, "[dist] a2=%d\n", a2 ? 1 : 0);
  CHECK(a2);
  std::uint64_t d1 = 0, c1 = 0;
  bool q1 = query_digest(cli, d1, c1);
  std::fprintf(stderr, "[dist] q1=%d c1=%llu\n", q1 ? 1 : 0, static_cast<unsigned long long>(c1));
  CHECK(q1);
  CHECK_EQ(c1, 2u);
  cli.close();

  void* wk_h = nullptr;
  std::string wargs = "--port " + std::to_string(port) + " --boot 200 --mode killsleep";
  CHECK(spawn(worker, wargs, &wk_h));
  wait_ms(1800);
  terminate(wk_h);
  wait_ms(400);

  Socket stale;
  CHECK(connect_retry(stale, port));
  CHECK(send_register(stale, 999));
  CHECK(!send_append(stale, mk_append(50, 100, 1, 200, 50, EfficiencyClass::kUseful,
                                      EventType::kOutputPublished, AttemptOutcome::kSucceeded)));
  stale.close();

  void* wk2 = nullptr;
  std::string wargs2 = "--port " + std::to_string(port) + " --boot 300 --mode append";
  CHECK(spawn(worker, wargs2, &wk2));
  wait_ms(600);
  terminate(wk2);
  wait_ms(200);

  Socket q;
  CHECK(connect_retry(q, port));
  std::uint64_t d2 = 0, c2 = 0;
  CHECK(query_digest(q, d2, c2));
  std::fprintf(stderr, "[dist] c2=%llu file_exists=%d\n", static_cast<unsigned long long>(c2),
               std::filesystem::exists(state) ? 1 : 0);
  CHECK_EQ(c2, 4u);
  q.close();

  terminate(coord_h);
  wait_ms(400);

  void* coord2 = nullptr;
  std::string cargs2 = "--port " + std::to_string(port) + " --path \"" + state + "\" --load --epoch 2";
  CHECK(spawn(coord, cargs2, &coord2));
  Socket r;
  CHECK(connect_retry(r, port));

  std::uint64_t d3 = 0, c3 = 0;
  CHECK(query_digest(r, d3, c3));
  CHECK_EQ(d3, d2);
  CHECK_EQ(c3, 4u);

  CHECK(send_register(r, 400));
  LedgerEntry fresh = mk_append(60, 180, 9, 400, 60, EfficiencyClass::kUseful,
                                EventType::kOutputPublished, AttemptOutcome::kSucceeded);
  fresh.fences.epoch = CoordinatorEpoch(2);
  CHECK(send_append(r, fresh));

  LedgerEntry oldd = mk_append(61, 180, 9, 400, 61, EfficiencyClass::kUseful,
                               EventType::kOutputPublished, AttemptOutcome::kSucceeded);
  oldd.fences.epoch = CoordinatorEpoch(1);
  CHECK(!send_append(r, oldd));

  std::uint64_t d4 = 0, c4 = 0;
  CHECK(query_digest(r, d4, c4));
  CHECK_EQ(c4, 5u);
  r.close();

  terminate(coord2);
  wait_ms(200);
}

}  // namespace

int main() {
#ifdef _WIN32
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::printf("WSAStartup failed\n"); return 1; }
  run();
  WSACleanup();
#else
  std::printf("DISTRIBUTED PROOF SKIPPED: requires Windows process/tcp API\n");
#endif
  TEST_MAIN_END();
}
