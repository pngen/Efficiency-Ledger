#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include "efficiency_ledger/error.hpp"

namespace el {

// Strongly typed resource dimensions. Percentages are never stored; they are
// derived views over these exact totals.
enum class ResourceDimension : std::uint8_t {
  kAcceleratorNanoseconds = 0,
  kCpuNanoseconds,
  kEnergyMicroJoules,
  kTransferBytes,
  kStorageBytes,
  kMemoryByteNanoseconds,
  kAcceleratorMemoryByteNanoseconds,
  kHostMemoryByteNanoseconds,
  kPinnedMemoryByteNanoseconds,
  kNetworkByteNanoseconds,
  kOperations,
  kTokens,
  kRequests,
  kWorkUnits,
  kCapacityByteNanoseconds,
  kDeviceNanoseconds,
  kDimensions_Count,  // sentinel; must remain last
};

inline constexpr std::size_t kDimensionCount =
    static_cast<std::size_t>(ResourceDimension::kDimensions_Count);

constexpr bool is_resource_dimension(std::size_t d) noexcept {
  return d < kDimensionCount;
}

inline const char* to_string(ResourceDimension d) noexcept {
  switch (d) {
    case ResourceDimension::kAcceleratorNanoseconds: return "AcceleratorNanoseconds";
    case ResourceDimension::kCpuNanoseconds: return "CpuNanoseconds";
    case ResourceDimension::kEnergyMicroJoules: return "EnergyMicroJoules";
    case ResourceDimension::kTransferBytes: return "TransferBytes";
    case ResourceDimension::kStorageBytes: return "StorageBytes";
    case ResourceDimension::kMemoryByteNanoseconds: return "MemoryByteNanoseconds";
    case ResourceDimension::kAcceleratorMemoryByteNanoseconds: return "AcceleratorMemoryByteNanoseconds";
    case ResourceDimension::kHostMemoryByteNanoseconds: return "HostMemoryByteNanoseconds";
    case ResourceDimension::kPinnedMemoryByteNanoseconds: return "PinnedMemoryByteNanoseconds";
    case ResourceDimension::kNetworkByteNanoseconds: return "NetworkByteNanoseconds";
    case ResourceDimension::kOperations: return "Operations";
    case ResourceDimension::kTokens: return "Tokens";
    case ResourceDimension::kRequests: return "Requests";
    case ResourceDimension::kWorkUnits: return "WorkUnits";
    case ResourceDimension::kCapacityByteNanoseconds: return "CapacityByteNanoseconds";
    case ResourceDimension::kDeviceNanoseconds: return "DeviceNanoseconds";
    case ResourceDimension::kDimensions_Count: return "<sentinel>";
  }
  return "<unknown>";
}

// Checked unsigned arithmetic. No counter may silently wrap.
inline bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (a > std::numeric_limits<std::uint64_t>::max() - b) {
    return false;
  }
  out = a + b;
  return true;
}

inline bool checked_sub(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (b > a) {
    return false;
  }
  out = a - b;
  return true;
}

inline bool checked_mul(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
    return false;
  }
  out = a * b;
  return true;
}

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

// Exact (a*b)/d computed with full 128-bit intermediate precision. Returns the
// quotient and writes the remainder to `rem`. `d` must be non-zero. This never
// overflows and never wraps; it is used for exact proportional splits.
inline std::uint64_t mul_div(std::uint64_t a, std::uint64_t b, std::uint64_t d,
                             std::uint64_t& rem) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
  unsigned __int64 hi = 0;
  unsigned __int64 lo = _umul128(a, b, &hi);
  unsigned __int64 rem64 = 0;
  unsigned __int64 q = _udiv128(hi, lo, d, &rem64);
  rem = rem64;
  return q;
#elif defined(__SIZEOF_INT128__)
  unsigned __int128 p = static_cast<unsigned __int128>(a) * b;
  rem = static_cast<std::uint64_t>(p % d);
  return static_cast<std::uint64_t>(p / d);
#else
  // 128-bit intermediate precision is required for exact proportional splits;
  // it is universally available on modern compiler targets (MSVC x64 intrinsics
  // or ISO __int128 on GCC/Clang).
#error "mul_div requires 128-bit integer support (MSVC x64 intrinsics or __int128)"
#endif
}

// ResourceUsage holds exact, non-negative per-dimension quantities. All
// arithmetic is checked and never wraps. A zero-initialized usage is "empty".
class ResourceUsage {
 public:
  explicit ResourceUsage(ResourceDimension d = ResourceDimension::kAcceleratorNanoseconds,
                         std::uint64_t amount = 0) noexcept {
    if (is_resource_dimension(static_cast<std::size_t>(d))) {
      data_[static_cast<std::size_t>(d)] = amount;
    }
  }

  std::uint64_t get(ResourceDimension d) const noexcept {
    return data_[static_cast<std::size_t>(d)];
  }

  // Set a dimension (non-negative). Invalid dimension is ignored.
  void set(ResourceDimension d, std::uint64_t amount) noexcept {
    if (is_resource_dimension(static_cast<std::size_t>(d))) {
      data_[static_cast<std::size_t>(d)] = amount;
    }
  }

  // Add a quantity; returns false on overflow (state is unchanged).
  bool add(ResourceDimension d, std::uint64_t amount) noexcept {
    auto& cell = data_[static_cast<std::size_t>(d)];
    std::uint64_t out = 0;
    if (!checked_add(cell, amount, out)) {
      return false;
    }
    cell = out;
    return true;
  }

  // Subtract a quantity; returns false if it would go negative (state unchanged).
  bool sub(ResourceDimension d, std::uint64_t amount) noexcept {
    auto& cell = data_[static_cast<std::size_t>(d)];
    std::uint64_t out = 0;
    if (!checked_sub(cell, amount, out)) {
      return false;
    }
    cell = out;
    return true;
  }

  // Elementwise addition of another usage; returns false on overflow (state
  // unchanged if any add fails). The caller may use a transactional copy.
  bool add(const ResourceUsage& other) noexcept {
    for (std::size_t i = 0; i < kDimensionCount; ++i) {
      std::uint64_t out = 0;
      if (!checked_add(data_[i], other.data_[i], out)) {
        return false;
      }
      data_[i] = out;
    }
    return true;
  }

  bool is_zero() const noexcept {
    for (const auto v : data_) {
      if (v != 0) return false;
    }
    return true;
  }

  bool valid() const noexcept {
    for (const auto v : data_) {
      // non-negative is guaranteed by the type; nothing further to validate.
      (void)v;
    }
    return true;
  }

  const std::array<std::uint64_t, kDimensionCount>& data() const noexcept { return data_; }
  std::array<std::uint64_t, kDimensionCount>& data() noexcept { return data_; }

  bool operator==(const ResourceUsage&) const noexcept = default;

  // Dimension-wise effective multiply for byte-time reconstructions; returns
  // false on overflow without mutating state.
  bool mul(ResourceDimension d, std::uint64_t factor) noexcept {
    auto& cell = data_[static_cast<std::size_t>(d)];
    std::uint64_t out = 0;
    if (!checked_mul(cell, factor, out)) {
      return false;
    }
    cell = out;
    return true;
  }

 private:
  std::array<std::uint64_t, kDimensionCount> data_{};
};

}  // namespace el
