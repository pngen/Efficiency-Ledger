#include "efficiency_ledger/protocol.hpp"

#include <cstring>

#include "efficiency_ledger/crc32c.hpp"

namespace el {

namespace {

void put_u32(std::string& s, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
}
void put_u64(std::string& s, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
}
bool get_u32(const std::string& s, std::size_t pos, std::uint32_t& out) {
  if (pos + 4 > s.size()) return false;
  out = static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[pos])) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[pos + 1])) << 8) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[pos + 2])) << 16) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[pos + 3])) << 24);
  return true;
}
bool get_u64(const std::string& s, std::size_t pos, std::uint64_t& out) {
  if (pos + 8 > s.size()) return false;
  out = 0;
  for (int i = 0; i < 8; ++i)
    out |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(s[pos + i])) << (8 * i);
  return true;
}

}  // namespace

bool encode_frame(std::uint32_t kind, std::uint64_t request_id, const std::string& body,
                  std::string& out) {
  std::string payload;
  payload.reserve(16 + body.size());
  put_u32(payload, kProtocolVersion);
  put_u32(payload, kind);
  put_u64(payload, request_id);
  payload.append(body);
  if (payload.size() > kMaxFramePayload) return false;

  out.clear();
  put_u32(out, static_cast<std::uint32_t>(payload.size()));
  const std::uint32_t crc = crc32c(payload.data(), payload.size());
  put_u32(out, crc);
  out.append(payload);
  return true;
}

bool decode_frame(const std::string& in, std::uint32_t& kind, std::uint64_t& request_id,
                  std::string& body) {
  if (in.size() < 8) return false;
  std::uint32_t payload_len = 0;
  if (!get_u32(in, 0, payload_len)) return false;
  if (payload_len > kMaxFramePayload) return false;
  if (payload_len + 8u != in.size()) return false;  // reject trailing/oversized/truncated
  std::uint32_t crc = 0;
  if (!get_u32(in, 4, crc)) return false;
  const std::string payload = in.substr(8, payload_len);
  if (crc32c(payload.data(), payload.size()) != crc) return false;

  std::uint32_t version = 0;
  if (!get_u32(payload, 0, version)) return false;
  if (version != kProtocolVersion) return false;
  if (!get_u32(payload, 4, kind)) return false;
  if (!get_u64(payload, 8, request_id)) return false;
  if (payload.size() >= 16) body = payload.substr(16);
  else body.clear();
  return true;
}

void encode_status(const Status& st, std::string& out) {
  out.clear();
  put_u32(out, static_cast<std::uint32_t>(st.code()));
  put_u32(out, static_cast<std::uint32_t>(st.message().size()));
  out.append(st.message());
}

void encode_status_payload(const Status& st, const std::string& payload, std::string& out) {
  out.clear();
  put_u32(out, static_cast<std::uint32_t>(st.code()));
  put_u32(out, static_cast<std::uint32_t>(st.message().size()));
  out.append(st.message());
  out.append(payload);
}

bool decode_status(const std::string& in, Status& st, std::string& payload) {
  if (in.size() < 8) return false;
  std::uint32_t code = 0, msg_len = 0;
  if (!get_u32(in, 0, code)) return false;
  if (!get_u32(in, 4, msg_len)) return false;
  if (8u + msg_len > in.size()) return false;
  const std::string msg = in.substr(8, msg_len);
  st = Status(static_cast<ErrorCode>(code), msg);
  if (8u + msg_len < in.size()) payload = in.substr(8 + msg_len);
  else payload.clear();
  return true;
}

}  // namespace el
