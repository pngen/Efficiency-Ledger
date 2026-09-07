#pragma once

#include <cstdint>
#include <string_view>

namespace el {

// Library version (semver).
inline constexpr std::uint32_t kVersionMajor = 1;
inline constexpr std::uint32_t kVersionMinor = 0;
inline constexpr std::uint32_t kVersionPatch = 1;

inline constexpr std::string_view kVersionString = "1.0.1";

// Namespace / branding for provenance and reports.
inline constexpr std::string_view kVendor = "Summon Software Labs";
inline constexpr std::string_view kProduct = "Efficiency Ledger";

// Canonical format version for persistent ledger files. The on-disk format is
// versioned; an unknown major/minor version must be rejected on load.
inline constexpr std::uint32_t kPersistenceFormatVersion = 1;

}  // namespace el
