#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

namespace el {

// Strong, non-interchangeable identity type. Two identity types with different
// tags are distinct C++ types and cannot be silently interchanged (the integer
// values are never shared across authority domains). A zero value is invalid.
template <typename Tag>
class Identity {
 public:
  using TagType = Tag;
  using ValueType = std::uint64_t;

  constexpr Identity() noexcept = default;
  constexpr explicit Identity(std::uint64_t v) noexcept : value_(v) {}

  constexpr static Identity invalid() noexcept { return Identity{}; }
  constexpr bool valid() const noexcept { return value_ != 0; }
  constexpr std::uint64_t value() const noexcept { return value_; }

  constexpr explicit operator bool() const noexcept { return valid(); }

  constexpr bool operator==(const Identity&) const noexcept = default;
  constexpr bool operator!=(const Identity&) const noexcept = default;
  constexpr bool operator<(const Identity& o) const noexcept { return value_ < o.value_; }
  constexpr bool operator<=(const Identity& o) const noexcept { return value_ <= o.value_; }
  constexpr bool operator>(const Identity& o) const noexcept { return value_ > o.value_; }
  constexpr bool operator>=(const Identity& o) const noexcept { return value_ >= o.value_; }

  std::string to_string() const { return std::to_string(value_); }

 private:
  std::uint64_t value_ = 0;
};

// Validation helper: an identity is valid iff its raw value is non-zero.
template <typename Tag>
constexpr bool valid(const Identity<Tag>& id) noexcept {
  return id.valid();
}

// Identity tags. Each tag is a distinct, non-interchangeable authority domain.
struct CoordinatorEpochTag {};
struct LedgerIdTag {};
struct LedgerGenerationTag {};
struct AccountIdTag {};
struct AccountGenerationTag {};
struct ServiceIdTag {};
struct ServiceGenerationTag {};
struct WorkloadIdTag {};
struct WorkloadGenerationTag {};
struct RequestIdTag {};
struct RequestGenerationTag {};
struct JobIdTag {};
struct JobGenerationTag {};
struct AttemptIdTag {};
struct AttemptGenerationTag {};
struct WorkerIdTag {};
struct WorkerBootIdTag {};
struct ResourceIdTag {};
struct ResourceGenerationTag {};
struct DeviceIdTag {};
struct DeviceGenerationTag {};
struct AllocationIdTag {};
struct AllocationGenerationTag {};
struct ReservationIdTag {};
struct ReservationGenerationTag {};
struct TransferIdTag {};
struct TransferGenerationTag {};
struct RecoveryIdTag {};
struct RecoveryGenerationTag {};
struct CacheObjectIdTag {};
struct CacheGenerationTag {};
struct EvidenceIdTag {};
struct EvidenceGenerationTag {};
struct AccountingEntryIdTag {};
struct AccountingGenerationTag {};
struct CommitIdTag {};
struct PublicationIdTag {};

using CoordinatorEpoch = Identity<CoordinatorEpochTag>;
using LedgerId = Identity<LedgerIdTag>;
using LedgerGeneration = Identity<LedgerGenerationTag>;
using AccountId = Identity<AccountIdTag>;
using AccountGeneration = Identity<AccountGenerationTag>;
using ServiceId = Identity<ServiceIdTag>;
using ServiceGeneration = Identity<ServiceGenerationTag>;
using WorkloadId = Identity<WorkloadIdTag>;
using WorkloadGeneration = Identity<WorkloadGenerationTag>;
using RequestId = Identity<RequestIdTag>;
using RequestGeneration = Identity<RequestGenerationTag>;
using JobId = Identity<JobIdTag>;
using JobGeneration = Identity<JobGenerationTag>;
using AttemptId = Identity<AttemptIdTag>;
using AttemptGeneration = Identity<AttemptGenerationTag>;
using WorkerId = Identity<WorkerIdTag>;
using WorkerBootId = Identity<WorkerBootIdTag>;
using ResourceId = Identity<ResourceIdTag>;
using ResourceGeneration = Identity<ResourceGenerationTag>;
using DeviceId = Identity<DeviceIdTag>;
using DeviceGeneration = Identity<DeviceGenerationTag>;
using AllocationId = Identity<AllocationIdTag>;
using AllocationGeneration = Identity<AllocationGenerationTag>;
using ReservationId = Identity<ReservationIdTag>;
using ReservationGeneration = Identity<ReservationGenerationTag>;
using TransferId = Identity<TransferIdTag>;
using TransferGeneration = Identity<TransferGenerationTag>;
using RecoveryId = Identity<RecoveryIdTag>;
using RecoveryGeneration = Identity<RecoveryGenerationTag>;
using CacheObjectId = Identity<CacheObjectIdTag>;
using CacheGeneration = Identity<CacheGenerationTag>;
using EvidenceId = Identity<EvidenceIdTag>;
using EvidenceGeneration = Identity<EvidenceGenerationTag>;
using AccountingEntryId = Identity<AccountingEntryIdTag>;
using AccountingGeneration = Identity<AccountingGenerationTag>;
using CommitId = Identity<CommitIdTag>;
using PublicationId = Identity<PublicationIdTag>;

}  // namespace el

namespace std {
template <typename Tag>
struct hash<el::Identity<Tag>> {
  std::size_t operator()(const el::Identity<Tag>& id) const noexcept {
    return std::hash<std::uint64_t>{}(id.value());
  }
};
}  // namespace std
