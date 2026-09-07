#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "efficiency_ledger/attribution.hpp"
#include "efficiency_ledger/enums.hpp"
#include "efficiency_ledger/error.hpp"
#include "efficiency_ledger/id.hpp"
#include "efficiency_ledger/resource.hpp"

namespace el {

// Typed subcategory. Exactly one member is meaningful, selected by the entry's
// EfficiencyClass. The others remain at a benign default; only the relevant one
// is read. Keeping them strongly typed (never opaque strings) preserves audit.
struct Subcategory {
  UsefulReason useful = UsefulReason::kAuthoritativeExecution;
  OverheadReason overhead = OverheadReason::kRequiredTransfer;
  WasteReason waste = WasteReason::kFailedAttempt;
  AvoidedWorkReason avoided = AvoidedWorkReason::kCacheHit;
  StrandedReason stranded = StrandedReason::kFragmentation;
};

struct AuthorityFences {
  CoordinatorEpoch epoch;                 // 0 == unset
  WorkerBootId worker_boot;               // 0 == unset
  LedgerGeneration ledger_generation;     // 0 == unset
  AccountGeneration account_generation;   // 0 == unset
  WorkloadGeneration workload_generation; // 0 == unset
  ResourceGeneration resource_generation; // 0 == unset
  ReservationGeneration reservation_generation;  // 0 == unset
  AllocationGeneration allocation_generation;    // 0 == unset
  TransferGeneration transfer_generation;        // 0 == unset
  RecoveryGeneration recovery_generation;        // 0 == unset
  CacheGeneration cache_generation;              // 0 == unset
  EvidenceGeneration evidence_generation;        // 0 == unset
  AccountingGeneration accounting_generation;    // 0 == unset
  PublicationId publication;                     // 0 == unset
};

// A reference to a specific resource/transfer/reservation/allocation/recovery/
// cache object whose lifetime is being accounted. Zero == unset.
struct Owners {
  ResourceId resource;
  TransferId transfer;
  ReservationId reservation;
  AllocationId allocation;
  RecoveryId recovery;
  CacheObjectId cache_object;
  DeviceId device;
  RequestId request;
  WorkloadId workload;
  JobId job;
  ServiceId service;
};

// Attempt-scoped authority. id == 0 means the entry is not attempt-scoped.
struct AttemptOptions {
  AttemptOptions() = default;
  AttemptOptions(AttemptId id_, AttemptGeneration gen_) : id(id_), generation(gen_) {}

  bool attached() const noexcept { return id.valid(); }

  AttemptId id;
  AttemptGeneration generation;
};

// The append-only, immutable accounting unit. Physical consumption is always
// held distinctly from classification; a later event may reinterpret an entry
// through an explicit reclassification event, never by mutating this record.
struct LedgerEntry {
  AccountingEntryId id;                 // stable, deduplicated identity
  EventType event = EventType::kExecutionCompleted;
  EfficiencyClass classification = EfficiencyClass::kUnknown;
  Subcategory subcategory;
  ResourceUsage usage;                  // physical consumption
  std::optional<ResourceUsage> avoided_work;       // counterfactual benefit
  std::optional<ResourceUsage> stranded_capacity;  // non-consumed capacity
  Provenance provenance = Provenance::kUnknown;
  Freshness freshness = Freshness::kCurrent;
  AccountId account;                    // primary owner account
  std::optional<SharedRef> shared;      // shared-work attribution (optional)
  AuthorityFences fences;
  Owners owners;
  AttemptOptions attempt;               // attempt-scoped authority
  AttemptOutcome attempt_outcome = AttemptOutcome::kPending;
  // For kReclassify revision events: the stable id of the entry being reinterpreted.
  std::optional<AccountingEntryId> revision_target;
  std::string note;                     // auxiliary explanation (not state)

  bool has_attempt() const noexcept { return attempt.id.valid(); }
  bool has_outcome() const noexcept { return attempt_outcome != AttemptOutcome::kPending; }
  bool is_terminal_outcome() const noexcept {
    return attempt_outcome != AttemptOutcome::kPending;
  }

  // Validate malformed input before any accounting.
  bool validate() const noexcept {
    if (!id.valid()) return false;
    // Reject invalid/out-of-range enums so a malformed frame cannot inject a
    // bogus classification, provenance, freshness, event type, or outcome.
    if (static_cast<unsigned>(event) > static_cast<unsigned>(EventType::kReclassify)) return false;
    if (static_cast<unsigned>(classification) > static_cast<unsigned>(EfficiencyClass::kUnknown)) return false;
    if (static_cast<unsigned>(provenance) > static_cast<unsigned>(Provenance::kUnknown)) return false;
    if (static_cast<unsigned>(freshness) > static_cast<unsigned>(Freshness::kUnknown)) return false;
    if (static_cast<unsigned>(attempt_outcome) > static_cast<unsigned>(AttemptOutcome::kSuperseded)) return false;
    if (!usage.valid()) return false;
    if (avoided_work && !avoided_work->valid()) return false;
    if (stranded_capacity && !stranded_capacity->valid()) return false;
    if (shared && !shared->valid()) return false;
    // Attempt-scoped events require a valid attempt id.
    if (has_outcome() && !attempt.id.valid()) return false;
    if (!account.valid() && !shared) {
      // Without a primary account and without shared participants there is no
      // owner; reject to avoid silently dropping the entry.
      return false;
    }
    return true;
  }
};

}  // namespace el
