#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace el {

// Canonical 64-bit digest over a ledger history. Deterministic: it depends only
// on the ordered canonical event stream and not on memory addresses, wall-clock
// noise, or unordered container iteration. Replaying the same history yields the
// same digest; any change to the ordered stream changes it.
struct Digest {
  std::uint64_t value = 0;

  bool operator==(const Digest&) const noexcept = default;
  bool operator!=(const Digest&) const noexcept = default;

  std::string hex() const;
  explicit operator bool() const noexcept { return value != 0; }
};

// A streaming, deterministic, order-sensitive hash accumulator (FNV-1a 64-bit
// over big-endian encoded bytes). It does not depend on instance state.
class HashAccumulator {
 public:
  HashAccumulator() = default;
  explicit HashAccumulator(std::uint64_t seed) : state_(seed) {}

  void mix(std::uint8_t b) noexcept;
  void mix16(std::uint16_t v) noexcept { mix_bytes(reinterpret_cast<const std::uint8_t*>(&v), 2); }
  void mix32(std::uint32_t v) noexcept { mix_bytes(reinterpret_cast<const std::uint8_t*>(&v), 4); }
  void mix64(std::uint64_t v) noexcept { mix_bytes(reinterpret_cast<const std::uint8_t*>(&v), 8); }

  void mix_bytes(const std::uint8_t* p, std::size_t n) noexcept;
  void mix(std::string_view s) noexcept;

  std::uint64_t value() const noexcept { return state_; }
  Digest digest() const noexcept { return Digest{state_}; }

 private:
  std::uint64_t state_ = 1469598103934665603ULL;  // FNV-1a 64 offset basis
};

}  // namespace el
