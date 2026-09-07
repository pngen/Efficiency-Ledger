#pragma once

#include <cstddef>
#include <cstdint>

namespace el {

// CRC-32C (Castagnoli) over a byte buffer. Used to integrity-check persistence.
// `seed` allows chaining; pass 0 (or the previous value) to continue a stream.
std::uint32_t crc32c(const void* data, std::size_t len, std::uint32_t seed = 0) noexcept;

}  // namespace el
