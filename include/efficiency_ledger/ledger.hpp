#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "efficiency_ledger/attribution.hpp"
#include "efficiency_ledger/digest.hpp"
#include "efficiency_ledger/entry.hpp"
#include "efficiency_ledger/enums.hpp"
#include "efficiency_ledger/error.hpp"
#include "efficiency_ledger/id.hpp"
#include "efficiency_ledger/resource.hpp"

namespace el {

// Per-dimension counts. Arrays are indexed by ResourceDimension and are always
// non-negative (checked arithmetic on entry).
using DimTotals = std::array<std::uint64_t, kDimensionCount>;

// Reserved system account for the explicit attribution residual. Every entry's
// physical total is attributed exactly to its beneficiary accounts plus (when
// the policy leaves a remainder) this residual account, so the global total
// always closes with no unexplained loss.
inline constexpr std::uint64_t kResidualAccountValue = 0xFFFFFFFFFFFFFFFFull;
inline constexpr LedgerId kDefaultLedgerId{1};

namespace detail {

// Per-account aggregation. Only USEFUL + NECESSARY_OVERHEAD + AVOIDABLE_WASTE +
// UNKNOWN contribute to physical; AVOIDED_WORK and STRANDED_CAPACITY are tracked
// as separate non-consumption quantities.
struct AccountAggregate {
  DimTotals physical{};
  DimTotals useful{};
  DimTotals overhead{};
  DimTotals waste{};
  DimTotals unknown{};
  DimTotals avoided{};
  DimTotals stranded{};
};

struct AttributionShare {
  AccountId account;
  DimTotals physical{};
  DimTotals avoided{};    // counterfactual benefit share
  DimTotals stranded{};   // non-consumed capacity share
};

struct AttributionResult {
  std::vector<AttributionShare> shares;
  DimTotals residual{};  // leftover physical (goes to kResidualAccount)
  bool invalid = false;
};

}  // namespace detail

struct AccountSummary {
  AccountId account;
  DimTotals physical{};
  DimTotals useful{};
  DimTotals overhead{};
  DimTotals waste{};
  DimTotals unknown{};
  DimTotals avoided{};
  DimTotals stranded{};
};

struct Reconciliation {
  bool closed = false;
  std::vector<bool> per_dimension_closed;
  DimTotals physical{};
  DimTotals classified{};  // useful + overhead + waste + unknown
  DimTotals unclassified{};  // physical - classified (must be 0 when closed)
  DimTotals avoided{};
  DimTotals stranded{};
  std::size_t entry_count = 0;
  std::vector<std::string> messages;
};

struct ReclassifyRequest {
  AccountingEntryId revision_id;   // stable id of the new revision event
  AccountingEntryId target;        // stable id of the entry being reinterpreted
  EfficiencyClass new_classification = EfficiencyClass::kUnknown;
  Subcategory new_subcategory;
  AuthorityFences fences;
  Provenance provenance = Provenance::kDerived;
  std::string note;
};

class EfficiencyLedger {
 public:
  EfficiencyLedger();
  explicit EfficiencyLedger(LedgerId id);

  // ---- Authority / generation state ----
  CoordinatorEpoch epoch() const;
  LedgerId ledger_id() const;
  LedgerGeneration ledger_generation() const;
  AccountGeneration account_generation() const;

  // Advance the coordinator epoch (real restart). Historical committed history
  // is preserved; dynamic current-resource evidence becomes revalidation-required.
  void advance_epoch();
  void set_epoch(CoordinatorEpoch epoch);
  void set_ledger_generation(LedgerGeneration gen);
  void set_account_generation(AccountGeneration gen);
  void set_system_generation(LedgerGeneration ledger_gen, AccountGeneration account_gen);

  void register_worker(WorkerBootId boot);
  void unregister_worker(WorkerBootId boot);
  bool has_worker(WorkerBootId boot) const;

  // ---- Append ----
  // Append an accounting entry. Validates input, fences authority, deduplicates
  // by stable entry id, applies shared attribution, and updates exact accounts.
  Status append(const LedgerEntry& entry);
  // Append a vector; each entry must be authoritative. Stops at the first error.
  Status append(const std::vector<LedgerEntry>& entries);

  // Reclassify an existing entry via an explicit append-only revision. The
  // original physical history is preserved; a later event may reinterpret it.
  Status reclassify(const ReclassifyRequest& req);

  // ---- Query ----
  bool contains(AccountingEntryId id) const;
  const LedgerEntry* get(AccountingEntryId id) const;
  std::size_t entry_count() const;
  std::vector<LedgerEntry> history() const;
  std::vector<AccountSummary> account_summaries() const;
  AccountSummary account_summary(AccountId account) const;
  bool has_account(AccountId account) const;

  // ---- Reconciliation / digest / replay ----
  Reconciliation reconcile() const;
  Digest digest() const;

  // ---- Persistence ----
  Status save(const std::filesystem::path& path) const;
  Status load(const std::filesystem::path& path);
  Status save_to_string(std::string& out) const;
  Status load_from_string(const std::string& bytes, std::string* err = nullptr);

  // Reconstruct state deterministically from a canonical history (same stream ->
  // same totals -> same digest). Existing state is replaced.
  Status load_history(const std::vector<LedgerEntry>& entries);

  // Closes accounting by resisting further mutation after terminal state; used
  // by shutdown/test races to assert no double-accounting.
  void set_shutting_down();
  bool shutting_down() const;

 private:
  Status append_locked(const LedgerEntry& entry);
  Status check_authority(const LedgerEntry& entry) const;
  detail::AttributionResult compute_attribution(const LedgerEntry& entry) const;
  Status apply_attribution(const LedgerEntry& entry, const detail::AttributionResult& attr);
  Status apply_reclassify_locked(const LedgerEntry& revision);

  mutable std::mutex mutex_;
  CoordinatorEpoch epoch_{1};
  LedgerId ledger_id_{kDefaultLedgerId};
  LedgerGeneration ledger_generation_{1};
  AccountGeneration account_generation_{1};
  bool shutting_down_ = false;
  bool epoch_advanced_ = false;
  // While true, historical reconstruction skips live-worker liveness fencing
  // (the workers are gone; their committed entries were already fenced).
  bool replaying_ = false;

  std::unordered_set<WorkerBootId> live_workers_;

  std::vector<LedgerEntry> history_;
  std::unordered_map<AccountingEntryId, std::size_t> index_;

  // Terminal attempts: an attempt id maps to its authoritative outcome and its
  // generation. A stale completion cannot rewrite it.
  struct AttemptState {
    AttemptOutcome outcome = AttemptOutcome::kPending;
    AttemptGeneration generation;
    PublicationId last_publication;
    bool authoritative = false;
  };
  std::unordered_map<AttemptId, AttemptState> attempts_;

  // At-most-once completion gate for transfer / reservation / allocation /
  // recovery lifecycle events (duplicate completions must not double-count).
  std::unordered_set<TransferId> transfer_completed_;
  std::unordered_set<ReservationId> reservation_released_;
  std::unordered_set<AllocationId> allocation_released_;
  std::unordered_set<RecoveryId> recovery_completed_;

  std::unordered_map<AccountId, detail::AccountAggregate> accounts_;
};

}  // namespace el
