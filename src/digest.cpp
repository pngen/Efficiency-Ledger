#include "efficiency_ledger/digest.hpp"

#include <cstdint>

namespace el {

namespace {
// FNV-1a prime for 64-bit.
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
}  // namespace

std::string Digest::hex() const {
  std::string out;
  out.reserve(16);
  const std::uint64_t v = value;
  for (int shift = 60; shift >= 0; shift -= 4) {
    const std::uint64_t nib = (v >> shift) & 0xF;
    out.push_back(nib < 10 ? static_cast<char>('0' + nib)
                           : static_cast<char>('a' + (nib - 10)));
  }
  return out;
}

void HashAccumulator::mix(std::uint8_t b) noexcept {
  state_ ^= b;
  state_ *= kFnvPrime;
}

void HashAccumulator::mix_bytes(const std::uint8_t* p, std::size_t n) noexcept {
  for (std::size_t i = 0; i < n; ++i) {
    state_ ^= p[i];
    state_ *= kFnvPrime;
  }
}

void HashAccumulator::mix(std::string_view s) noexcept {
  mix_bytes(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

}  // namespace el
