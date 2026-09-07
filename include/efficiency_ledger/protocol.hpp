#pragma once

#include <cstdint>
#include <string>

#include "efficiency_ledger/entry.hpp"
#include "efficiency_ledger/error.hpp"

namespace el {

// Wire protocol version. A mismatch is rejected before any accounting.
inline constexpr std::uint32_t kProtocolVersion = 1;

// Maximum accepted frame payload (bounds allocation before allocation).
inline constexpr std::uint32_t kMaxFramePayload = 1u << 20;  // 1 MiB

// Frame on the wire: [u32 payload_len][u32 crc32c(payload)][payload].
// payload = [u32 protocol_version][u32 message_kind][u64 request_id][body].
enum class MessageKind : std::uint32_t {
  kRegister = 1,      // body: u64 worker_boot
  kAppend = 2,        // body: entry bytes (canonical codec)
  kQueryDigest = 3,   // body: empty
  kQueryTotals = 4,   // body: u64 account_id? (0 == all)
  kReclassify = 5,    // body: revision entry bytes
  kShutdown = 6,      // body: empty
};

// Encode/decode a frame. Throws no exceptions over the network path; instead
// reports booleans so malformed/oversized input can be rejected without memory
// growth.
bool encode_frame(std::uint32_t kind, std::uint64_t request_id,
                  const std::string& body, std::string& out);

// Returns true and fills kind/request_id/body on success. Returns false if the
// provided buffer is malformed, oversized, or has a bad checksum.
bool decode_frame(const std::string& in, std::uint32_t& kind,
                  std::uint64_t& request_id, std::string& body);

// Encode a status response: [u32 code][u32 msg_len][msg].
void encode_status(const Status& st, std::string& out);
// Encode an ok status plus a payload blob.
void encode_status_payload(const Status& st, const std::string& payload, std::string& out);

// Append a status frame's body into a request body (used by workers for
// response framing). These mirror the frame helpers but for Status bodies.
bool decode_status(const std::string& in, Status& st, std::string& payload);

}  // namespace el
