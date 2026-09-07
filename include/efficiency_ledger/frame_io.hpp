#pragma once

#include <cstdint>
#include <string>

#include "efficiency_ledger/protocol.hpp"
#include "efficiency_ledger/socket.hpp"

namespace el {

// Partial-read/write-safe framed message exchange over a Socket.
inline bool write_frame(Socket& s, std::uint32_t kind, std::uint64_t req, const std::string& body) {
  std::string frame;
  if (!encode_frame(kind, req, body, frame)) return false;
  return s.send_all(frame);
}

inline bool read_frame(Socket& s, std::uint32_t& kind, std::uint64_t& req, std::string& body) {
  std::string header;
  if (!s.recv_all(header, 8)) return false;
  const std::uint32_t payload_len =
      static_cast<std::uint32_t>(static_cast<std::uint8_t>(header[0])) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(header[1])) << 8) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(header[2])) << 16) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(header[3])) << 24);
  if (payload_len > kMaxFramePayload) return false;
  std::string payload;
  if (!s.recv_all(payload, payload_len)) return false;
  // prepend the 8-byte header so decode_frame can validate length/CRC entirely
  std::string frame = header + payload;
  return decode_frame(frame, kind, req, body);
}

inline bool write_status(Socket& s, const Status& st, const std::string& payload) {
  std::string body;
  encode_status_payload(st, payload, body);
  return write_frame(s, 0, 0, body);
}

inline bool read_status(Socket& s, Status& st, std::string& payload) {
  std::uint32_t kind = 0;
  std::uint64_t req = 0;
  std::string body;
  if (!read_frame(s, kind, req, body)) return false;
  return decode_status(body, st, payload);
}

}  // namespace el
