#include "efficiency_ledger/crc32c.hpp"

namespace el {
namespace {

std::uint32_t table_[256];
bool table_built_ = false;

void build_table() noexcept {
  constexpr std::uint32_t kPolynomial = 0x82F63B78u;  // reflected CRC-32C polynomial
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t c = i;
    for (int k = 0; k < 8; ++k) {
      c = (c & 1u) ? (kPolynomial ^ (c >> 1)) : (c >> 1);
    }
    table_[i] = c;
  }
  table_built_ = true;
}

}  // namespace

std::uint32_t crc32c(const void* data, std::size_t len, std::uint32_t seed) noexcept {
  if (!table_built_) build_table();
  const auto* p = static_cast<const std::uint8_t*>(data);
  std::uint32_t crc = ~seed;
  for (std::size_t i = 0; i < len; ++i) {
    crc = table_[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
  }
  return ~crc;
}

}  // namespace el
