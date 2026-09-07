#pragma once

#include <cstdint>

namespace el {

// Canonical efficiency classes. These are disjoint classification buckets used
// for reconciliation: for a given resource dimension, physical consumption is
// exactly the sum of USEFUL + NECESSARY_OVERHEAD + AVOIDABLE_WASTE + UNKNOWN.
// AVOIDED_WORK and STRANDED_CAPACITY are tracked as separate, non-consumption
// quantities (they never enter physical-consumption reconciliation).
enum class EfficiencyClass : std::uint8_t {
  kUseful = 0,
  kNecessaryOverhead = 1,
  kAvoidableWaste = 2,
  kAvoidedWork = 3,
  kStrandedCapacity = 4,
  kUnknown = 5,
};

inline const char* to_string(EfficiencyClass c) noexcept {
  switch (c) {
    case EfficiencyClass::kUseful: return "USEFUL";
    case EfficiencyClass::kNecessaryOverhead: return "NECESSARY_OVERHEAD";
    case EfficiencyClass::kAvoidableWaste: return "AVOIDABLE_WASTE";
    case EfficiencyClass::kAvoidedWork: return "AVOIDED_WORK";
    case EfficiencyClass::kStrandedCapacity: return "STRANDED_CAPACITY";
    case EfficiencyClass::kUnknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

// Typed subcategories. They are separate enums so that reason codes are never
// encoded into opaque strings and never collapse to a single bucket.
enum class UsefulReason : std::uint8_t {
  kAuthoritativeExecution = 0,
  kAcceptedOutput,
  kCommittedProgress,
  kReusedValidState,
};

enum class OverheadReason : std::uint8_t {
  kRequiredTransfer = 0,
  kRequiredCheckpoint,
  kRequiredRecovery,
  kRequiredReplication,
  kRequiredWarmup,
  kRequiredCoordination,
};

enum class WasteReason : std::uint8_t {
  kFailedAttempt = 0,
  kDuplicateAttempt,
  kStaleAttempt,
  kRejectedCompletion,
  kCancelledAfterWork,
  kRecomputation,
  kDuplicateTransfer,
  kUnusedPrefetch,
  kInvalidReuse,
  kExpiredResidency,
  kAbandonedRecovery,
  kRolledBackWork,
  kSpeculationRejected,
  kPolicyMisprediction,
  kIdleReservedCapacity,
  kFragmentationLoss,
};

enum class AvoidedWorkReason : std::uint8_t {
  kCacheHit = 0,
  kPrefixReuse,
  kTensorReuse,
  kGraphReplay,
  kKernelReuse,
  kWarmResidency,
  kCheckpointRestore,
  kDeduplicatedTransfer,
  kSharedExecution,
};

enum class StrandedReason : std::uint8_t {
  kFragmentation = 0,
  kReservationIdle,
  kUnusableHeadroom,
  kTopologyMismatch,
  kIncompatibleDevice,
  kPinnedButUnused,
  kResidencyHold,
  kCapacityFenced,
  kResourceUnavailableByAuthority,
  kMemoryHeadroom,
  kWarmStandbyIdle,
  kCapacityWithoutDemand,
  kCapacityBlockedByConstraint,
};

// Provenance: how a quantity was obtained. Never silently treat estimated
// quantities as measured.
enum class Provenance : std::uint8_t {
  kMeasured = 0,
  kReported,
  kDerived,
  kEstimated,
  kReconstructed,
  kSynthetic,
  kPolicy,
  kUnknown,
};

inline const char* to_string(Provenance p) noexcept {
  switch (p) {
    case Provenance::kMeasured: return "MEASURED";
    case Provenance::kReported: return "REPORTED";
    case Provenance::kDerived: return "DERIVED";
    case Provenance::kEstimated: return "ESTIMATED";
    case Provenance::kReconstructed: return "RECONSTRUCTED";
    case Provenance::kSynthetic: return "SYNTHETIC";
    case Provenance::kPolicy: return "POLICY";
    case Provenance::kUnknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

// Freshness of dynamic evidence.
enum class Freshness : std::uint8_t {
  kCurrent = 0,
  kStale,
  kExpired,
  kRevalidationRequired,
  kHistorical,
  kUnknown,
};

inline const char* to_string(Freshness f) noexcept {
  switch (f) {
    case Freshness::kCurrent: return "CURRENT";
    case Freshness::kStale: return "STALE";
    case Freshness::kExpired: return "EXPIRED";
    case Freshness::kRevalidationRequired: return "REVALIDATION_REQUIRED";
    case Freshness::kHistorical: return "HISTORICAL";
    case Freshness::kUnknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}

// Resource event types. Only implemented semantics are used.
enum class EventType : std::uint8_t {
  kExecutionStarted = 0,
  kExecutionProgress,
  kExecutionCompleted,
  kOutputPublished,
  kOutputRejected,
  kAttemptFailed,
  kAttemptCancelled,
  kAttemptSuperseded,
  kRetryStarted,
  kTransferStarted,
  kTransferCompleted,
  kTransferDiscarded,
  kResidencyAcquired,
  kResidencyReleased,
  kReservationAcquired,
  kReservationReleased,
  kCacheHit,
  kCacheMiss,
  kStateReused,
  kStateRecomputed,
  kCheckpointWritten,
  kCheckpointRestored,
  kRecoveryStarted,
  kRecoveryCompleted,
  kRecoveryFailed,
  kAllocationAcquired,
  kAllocationReleased,
  kFragmentationObserved,
  kCapacityStranded,
  kCapacityRestored,
  kWorkAccepted,
  kWorkRejected,
  kCommitAuthorized,
  kCommitRejected,
  kClassify,        // classification revision / derived view
  kReclassify,      // reclassification revision event (append-only)
};

// Terminal outcome of an execution attempt. Once terminal, a stale completion
// and a later classification reversal cannot rewrite the authoritative outcome.
enum class AttemptOutcome : std::uint8_t {
  kPending = 0,
  kSucceeded,
  kFailed,
  kCancelled,
  kSuperseded,
};

inline const char* to_string(AttemptOutcome o) noexcept {
  switch (o) {
    case AttemptOutcome::kPending: return "PENDING";
    case AttemptOutcome::kSucceeded: return "SUCCEEDED";
    case AttemptOutcome::kFailed: return "FAILED";
    case AttemptOutcome::kCancelled: return "CANCELLED";
    case AttemptOutcome::kSuperseded: return "SUPERSEDED";
  }
  return "UNKNOWN";
}

}  // namespace el
