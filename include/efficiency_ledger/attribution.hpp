#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "efficiency_ledger/id.hpp"
#include "efficiency_ledger/error.hpp"
#include "efficiency_ledger/resource.hpp"

namespace el {

// Shared-work attribution policies. A policy is versioned and generation-bound;
// changing a policy produces a new derived view rather than silently rewriting
// historical accounting.
enum class AttributionPolicy : std::uint8_t {
  kDirectOwnership = 0,
  kEqualShare,
  kByBytes,
  kByTokens,
  kByExecutionTime,
  kByReservedCapacity,
  kByWeight,
  kUnattributedShared,
};

inline const char* to_string(AttributionPolicy p) noexcept {
  switch (p) {
    case AttributionPolicy::kDirectOwnership: return "DIRECT_OWNERSHIP";
    case AttributionPolicy::kEqualShare: return "EQUAL_SHARE";
    case AttributionPolicy::kByBytes: return "BY_BYTES";
    case AttributionPolicy::kByTokens: return "BY_TOKENS";
    case AttributionPolicy::kByExecutionTime: return "BY_EXECUTION_TIME";
    case AttributionPolicy::kByReservedCapacity: return "BY_RESERVED_CAPACITY";
    case AttributionPolicy::kByWeight: return "BY_WEIGHT";
    case AttributionPolicy::kUnattributedShared: return "UNATTRIBUTED_SHARED";
  }
  return "UNKNOWN";
}

// Upper bound on shared-work participants to bound allocation before allocation.
inline constexpr std::size_t kMaxSharedParticipants = 4096;

// A participant of shared work: the beneficiary account and its integer weight
// for the chosen policy.
struct SharedParticipant {
  AccountId account;
  std::uint64_t weight = 0;
};

// Exact shared-attribution descriptor. Weights are non-negative integers; the
// ledger distributes each resource dimension with largest-remainder rounding so
// that the attributed total plus the explicit residual equals the physical total
// exactly (no double-counting, no unexplained loss).
struct SharedRef {
  AttributionPolicy policy = AttributionPolicy::kEqualShare;
  std::vector<SharedParticipant> participants;

  bool valid() const noexcept {
    if (participants.empty()) return true;  // empty == direct ownership fallback
    if (participants.size() > kMaxSharedParticipants) return false;
    std::uint64_t total = 0;
    for (const auto& p : participants) {
      if (!p.account.valid()) return false;
      if (p.weight == 0) return false;
      if (!checked_add(total, p.weight, total)) return false;
    }
    if (participants.size() == 1) {
      // A single participant with weight > 0 is still valid; the weight is the
      // whole share.
      return true;
    }
    return total != 0;
  }
};

}  // namespace el
