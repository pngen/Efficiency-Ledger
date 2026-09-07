#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "efficiency_ledger/codec.hpp"
#include "efficiency_ledger/frame_io.hpp"
#include "efficiency_ledger/ledger.hpp"
#include "efficiency_ledger/socket.hpp"

using namespace el;

namespace {

void put_u64(std::string& s, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
}

std::uint64_t u64_from(const std::string& s) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(s[i])) << (8 * i);
  return v;
}

}  // namespace

int main(int argc, char** argv) {
  std::uint16_t port = 37123;
  std::string path;
  std::uint64_t epoch = 0;
  bool load = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    else if (a == "--path" && i + 1 < argc) path = argv[++i];
    else if (a == "--epoch" && i + 1 < argc) epoch = static_cast<std::uint64_t>(std::stoull(argv[++i]));
    else if (a == "--load") load = true;
  }

#ifdef _WIN32
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::fprintf(stderr, "WSAStartup failed\n"); return 1; }
#endif

  EfficiencyLedger led;
  if (load && !path.empty()) {
    Status lst = led.load(path);
    if (!lst.ok()) { std::fprintf(stderr, "load failed: %s\n", lst.message().c_str()); }
  }
  // A restart advances the coordinator epoch: historical committed history is
  // preserved, dynamic current-resource evidence becomes REVALIDATION_REQUIRED,
  // and old-epoch mutation traffic is rejected.
  if (epoch != 0) led.set_epoch(CoordinatorEpoch(epoch));

  Socket server;
  if (!server.bind_and_listen(port)) {
    std::fprintf(stderr, "bind failed\n");
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  bool running = true;
  while (running) {
    Socket conn;
    if (!server.accept(conn)) break;
    // Track the boot id registered on this connection; unregister on disconnect.
    WorkerBootId conn_boot;
    bool registered = false;
    while (true) {
      std::uint32_t kind = 0;
      std::uint64_t req = 0;
      std::string body;
      if (!read_frame(conn, kind, req, body)) {
        if (registered && conn_boot.valid()) led.unregister_worker(conn_boot);
        break;  // peer closed
      }
      if (kind == static_cast<std::uint32_t>(MessageKind::kRegister)) {
        if (body.size() >= 8) {
          conn_boot = WorkerBootId(u64_from(body.substr(0, 8)));
          led.register_worker(conn_boot);
          registered = true;
          write_status(conn, Status::success(), "");
        } else {
          write_status(conn, Status(ErrorCode::kProtocolError, "bad register"), "");
        }
      } else if (kind == static_cast<std::uint32_t>(MessageKind::kAppend) ||
                 kind == static_cast<std::uint32_t>(MessageKind::kReclassify)) {
        std::size_t off = 0;
        LedgerEntry e;
        if (decode_entry_bytes(body, off, e) && off == body.size()) {
          Status st = led.append(e);
          if (st.ok() && !path.empty()) {
            // Atomic save after every committed append (durable across kill/restart).
            led.save(path);
          }
          write_status(conn, st, "");
        } else {
          write_status(conn, Status(ErrorCode::kProtocolError, "bad entry"), "");
        }
      } else if (kind == static_cast<std::uint32_t>(MessageKind::kQueryDigest)) {
        const Digest d = led.digest();
        std::string payload;
        put_u64(payload, d.value);
        put_u64(payload, static_cast<std::uint64_t>(led.entry_count()));
        write_status(conn, Status::success(), payload);
      } else if (kind == static_cast<std::uint32_t>(MessageKind::kShutdown)) {
        if (!path.empty()) led.save(path);
        running = false;
        write_status(conn, Status::success(), "");
        break;
      } else {
        write_status(conn, Status(ErrorCode::kProtocolError, "unknown message"), "");
      }
    }
  }

  if (!path.empty()) led.save(path);
  server.close();
#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
