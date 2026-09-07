#include "efficiency_ledger/ledger.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>

#include "efficiency_ledger/crc32c.hpp"
#include "efficiency_ledger/version.hpp"

namespace el {

namespace {

constexpr char kMagic[4] = {'E', 'L', 'L', 'D'};
constexpr std::size_t kHeaderLen = 48;
constexpr std::size_t kFooterLen = 4;

void put_u8(std::string& s, std::uint8_t v) { s.push_back(static_cast<char>(v)); }
void put_u16(std::string& s, std::uint16_t v) {
  s.push_back(static_cast<char>(v & 0xFFu));
  s.push_back(static_cast<char>((v >> 8) & 0xFFu));
}
void put_u32(std::string& s, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
}
void put_u64(std::string& s, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
}

class Reader {
 public:
  explicit Reader(const std::string& b) : b_(b) {}

  bool u8(std::uint8_t& out) {
    if (pos_ + 1 > b_.size()) { ok_ = false; return false; }
    out = static_cast<std::uint8_t>(b_[pos_]);
    ++pos_;
    return true;
  }
  bool u16(std::uint16_t& out) {
    if (pos_ + 2 > b_.size()) { ok_ = false; return false; }
    std::uint16_t v = 0;
    for (int i = 0; i < 2; ++i)
      v |= static_cast<std::uint16_t>(static_cast<std::uint8_t>(b_[pos_ + i])) << (8 * i);
    out = v; pos_ += 2; return true;
  }
  bool u32(std::uint32_t& out) {
    if (pos_ + 4 > b_.size()) { ok_ = false; return false; }
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
      v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(b_[pos_ + i])) << (8 * i);
    out = v; pos_ += 4; return true;
  }
  bool u64(std::uint64_t& out) {
    if (pos_ + 8 > b_.size()) { ok_ = false; return false; }
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
      v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(b_[pos_ + i])) << (8 * i);
    out = v; pos_ += 8; return true;
  }
  bool bytes(std::uint8_t* dst, std::size_t n) {
    if (pos_ + n > b_.size()) { ok_ = false; return false; }
    std::memcpy(dst, b_.data() + pos_, n);
    pos_ += n; return true;
  }
  std::size_t remaining() const { return b_.size() - pos_; }
  bool ok() const { return ok_; }
  const char* ptr() const { return b_.data() + pos_; }
  void advance(std::size_t n) { pos_ += n; }

 private:
  const std::string& b_;
  std::size_t pos_ = 0;
  bool ok_ = true;
};

void encode_entry(const LedgerEntry& e, std::string& out) {
  put_u32(out, 0);  // patched below with the body length
  const std::size_t body_start = out.size();
  put_u64(out, e.id.value());
  put_u8(out, static_cast<std::uint8_t>(e.event));
  put_u8(out, static_cast<std::uint8_t>(e.classification));
  put_u8(out, static_cast<std::uint8_t>(e.subcategory.useful));
  put_u8(out, static_cast<std::uint8_t>(e.subcategory.overhead));
  put_u8(out, static_cast<std::uint8_t>(e.subcategory.waste));
  put_u8(out, static_cast<std::uint8_t>(e.subcategory.avoided));
  put_u8(out, static_cast<std::uint8_t>(e.subcategory.stranded));
  for (std::size_t i = 0; i < kDimensionCount; ++i) put_u64(out, e.usage.data()[i]);

  put_u8(out, e.avoided_work ? 1 : 0);
  if (e.avoided_work) {
    for (std::size_t i = 0; i < kDimensionCount; ++i) put_u64(out, e.avoided_work->data()[i]);
  }
  put_u8(out, e.stranded_capacity ? 1 : 0);
  if (e.stranded_capacity) {
    for (std::size_t i = 0; i < kDimensionCount; ++i) put_u64(out, e.stranded_capacity->data()[i]);
  }

  put_u8(out, static_cast<std::uint8_t>(e.provenance));
  put_u8(out, static_cast<std::uint8_t>(e.freshness));
  put_u64(out, e.account.value());

  put_u8(out, e.shared ? 1 : 0);
  if (e.shared) {
    put_u8(out, static_cast<std::uint8_t>(e.shared->policy));
    put_u32(out, static_cast<std::uint32_t>(e.shared->participants.size()));
    for (const auto& p : e.shared->participants) {
      put_u64(out, p.account.value());
      put_u64(out, p.weight);
    }
  }

  put_u64(out, e.fences.epoch.value());
  put_u64(out, e.fences.worker_boot.value());
  put_u64(out, e.fences.ledger_generation.value());
  put_u64(out, e.fences.account_generation.value());
  put_u64(out, e.fences.workload_generation.value());
  put_u64(out, e.fences.resource_generation.value());
  put_u64(out, e.fences.reservation_generation.value());
  put_u64(out, e.fences.allocation_generation.value());
  put_u64(out, e.fences.transfer_generation.value());
  put_u64(out, e.fences.recovery_generation.value());
  put_u64(out, e.fences.cache_generation.value());
  put_u64(out, e.fences.evidence_generation.value());
  put_u64(out, e.fences.accounting_generation.value());
  put_u64(out, e.fences.publication.value());

  put_u64(out, e.owners.resource.value());
  put_u64(out, e.owners.transfer.value());
  put_u64(out, e.owners.reservation.value());
  put_u64(out, e.owners.allocation.value());
  put_u64(out, e.owners.recovery.value());
  put_u64(out, e.owners.cache_object.value());
  put_u64(out, e.owners.device.value());
  put_u64(out, e.owners.request.value());
  put_u64(out, e.owners.workload.value());
  put_u64(out, e.owners.job.value());
  put_u64(out, e.owners.service.value());

  put_u64(out, e.attempt.id.value());
  put_u64(out, e.attempt.generation.value());
  put_u8(out, static_cast<std::uint8_t>(e.attempt_outcome));

  put_u8(out, e.revision_target ? 1 : 0);
  if (e.revision_target) put_u64(out, e.revision_target->value());

  put_u32(out, static_cast<std::uint32_t>(e.note.size()));
  out.append(e.note);

  const std::uint32_t body_len = static_cast<std::uint32_t>(out.size() - body_start);
  out[body_start - 4] = static_cast<char>(body_len & 0xFFu);
  out[body_start - 3] = static_cast<char>((body_len >> 8) & 0xFFu);
  out[body_start - 2] = static_cast<char>((body_len >> 16) & 0xFFu);
  out[body_start - 1] = static_cast<char>((body_len >> 24) & 0xFFu);
}

bool decode_entry(const std::string& buf, std::size_t& offset, LedgerEntry& e) {
  if (offset + 4 > buf.size()) return false;
  const std::uint32_t len =
      static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf[offset])) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf[offset + 1])) << 8) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf[offset + 2])) << 16) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf[offset + 3])) << 24);
  offset += 4;
  if (len > 1u << 26) return false;
  if (offset + len > buf.size()) return false;

  const std::string sub = buf.substr(offset, len);
  offset += len;
  Reader r(sub);

  std::uint64_t v = 0;
  if (!r.u64(v)) return false; e.id = AccountingEntryId(v);
  std::uint8_t b = 0;
  if (!r.u8(b)) return false; e.event = static_cast<EventType>(b);
  if (!r.u8(b)) return false; e.classification = static_cast<EfficiencyClass>(b);
  if (!r.u8(b)) return false; e.subcategory.useful = static_cast<UsefulReason>(b);
  if (!r.u8(b)) return false; e.subcategory.overhead = static_cast<OverheadReason>(b);
  if (!r.u8(b)) return false; e.subcategory.waste = static_cast<WasteReason>(b);
  if (!r.u8(b)) return false; e.subcategory.avoided = static_cast<AvoidedWorkReason>(b);
  if (!r.u8(b)) return false; e.subcategory.stranded = static_cast<StrandedReason>(b);
  for (std::size_t i = 0; i < kDimensionCount; ++i) { if (!r.u64(v)) return false; e.usage.data()[i] = v; }

  if (!r.u8(b)) return false;
  if (b == 1) {
    ResourceUsage u;
    for (std::size_t i = 0; i < kDimensionCount; ++i) { if (!r.u64(v)) return false; u.data()[i] = v; }
    e.avoided_work = u;
  } else if (b != 0) { return false; }
  if (!r.u8(b)) return false;
  if (b == 1) {
    ResourceUsage u;
    for (std::size_t i = 0; i < kDimensionCount; ++i) { if (!r.u64(v)) return false; u.data()[i] = v; }
    e.stranded_capacity = u;
  } else if (b != 0) { return false; }

  if (!r.u8(b)) return false; e.provenance = static_cast<Provenance>(b);
  if (!r.u8(b)) return false; e.freshness = static_cast<Freshness>(b);
  if (!r.u64(v)) return false; e.account = AccountId(v);

  if (!r.u8(b)) return false;
  if (b == 1) {
    SharedRef sh;
    std::uint8_t pol = 0;
    if (!r.u8(pol)) return false; sh.policy = static_cast<AttributionPolicy>(pol);
    std::uint32_t n = 0;
    if (!r.u32(n)) return false;
    if (n > kMaxSharedParticipants) return false;
    sh.participants.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
      std::uint64_t acc = 0, w = 0;
      if (!r.u64(acc)) return false;
      if (!r.u64(w)) return false;
      sh.participants.push_back({AccountId(acc), w});
    }
    e.shared = sh;
  } else if (b != 0) { return false; }

  if (!r.u64(v)) return false; e.fences.epoch = CoordinatorEpoch(v);
  if (!r.u64(v)) return false; e.fences.worker_boot = WorkerBootId(v);
  if (!r.u64(v)) return false; e.fences.ledger_generation = LedgerGeneration(v);
  if (!r.u64(v)) return false; e.fences.account_generation = AccountGeneration(v);
  if (!r.u64(v)) return false; e.fences.workload_generation = WorkloadGeneration(v);
  if (!r.u64(v)) return false; e.fences.resource_generation = ResourceGeneration(v);
  if (!r.u64(v)) return false; e.fences.reservation_generation = ReservationGeneration(v);
  if (!r.u64(v)) return false; e.fences.allocation_generation = AllocationGeneration(v);
  if (!r.u64(v)) return false; e.fences.transfer_generation = TransferGeneration(v);
  if (!r.u64(v)) return false; e.fences.recovery_generation = RecoveryGeneration(v);
  if (!r.u64(v)) return false; e.fences.cache_generation = CacheGeneration(v);
  if (!r.u64(v)) return false; e.fences.evidence_generation = EvidenceGeneration(v);
  if (!r.u64(v)) return false; e.fences.accounting_generation = AccountingGeneration(v);
  if (!r.u64(v)) return false; e.fences.publication = PublicationId(v);

  if (!r.u64(v)) return false; e.owners.resource = ResourceId(v);
  if (!r.u64(v)) return false; e.owners.transfer = TransferId(v);
  if (!r.u64(v)) return false; e.owners.reservation = ReservationId(v);
  if (!r.u64(v)) return false; e.owners.allocation = AllocationId(v);
  if (!r.u64(v)) return false; e.owners.recovery = RecoveryId(v);
  if (!r.u64(v)) return false; e.owners.cache_object = CacheObjectId(v);
  if (!r.u64(v)) return false; e.owners.device = DeviceId(v);
  if (!r.u64(v)) return false; e.owners.request = RequestId(v);
  if (!r.u64(v)) return false; e.owners.workload = WorkloadId(v);
  if (!r.u64(v)) return false; e.owners.job = JobId(v);
  if (!r.u64(v)) return false; e.owners.service = ServiceId(v);

  if (!r.u64(v)) return false; e.attempt.id = AttemptId(v);
  if (!r.u64(v)) return false; e.attempt.generation = AttemptGeneration(v);
  if (!r.u8(b)) return false; e.attempt_outcome = static_cast<AttemptOutcome>(b);

  if (!r.u8(b)) return false;
  if (b == 1) {
    if (!r.u64(v)) return false; e.revision_target = AccountingEntryId(v);
  } else if (b != 0) { return false; }

  std::uint32_t note_len = 0;
  if (!r.u32(note_len)) return false;
  if (note_len > (1u << 20)) return false;
  if (r.remaining() != note_len) return false;
  if (note_len > 0) {
    std::string note(note_len, '\0');
    if (!r.bytes(reinterpret_cast<std::uint8_t*>(&note[0]), note_len)) return false;
    e.note = std::move(note);
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / authority state
// ---------------------------------------------------------------------------

EfficiencyLedger::EfficiencyLedger() : EfficiencyLedger(kDefaultLedgerId) {}
EfficiencyLedger::EfficiencyLedger(LedgerId id) { ledger_id_ = id; }

CoordinatorEpoch EfficiencyLedger::epoch() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return epoch_;
}
LedgerId EfficiencyLedger::ledger_id() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return ledger_id_;
}
LedgerGeneration EfficiencyLedger::ledger_generation() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return ledger_generation_;
}
AccountGeneration EfficiencyLedger::account_generation() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return account_generation_;
}

void EfficiencyLedger::advance_epoch() {
  std::lock_guard<std::mutex> lk(mutex_);
  epoch_ = CoordinatorEpoch(epoch_.value() + 1);
  epoch_advanced_ = true;
}
void EfficiencyLedger::set_epoch(CoordinatorEpoch epoch) {
  std::lock_guard<std::mutex> lk(mutex_);
  epoch_ = epoch;
}
void EfficiencyLedger::set_ledger_generation(LedgerGeneration gen) {
  std::lock_guard<std::mutex> lk(mutex_);
  ledger_generation_ = gen;
}
void EfficiencyLedger::set_account_generation(AccountGeneration gen) {
  std::lock_guard<std::mutex> lk(mutex_);
  account_generation_ = gen;
}
void EfficiencyLedger::set_system_generation(LedgerGeneration ledger_gen,
                                             AccountGeneration account_gen) {
  std::lock_guard<std::mutex> lk(mutex_);
  ledger_generation_ = ledger_gen;
  account_generation_ = account_gen;
}

void EfficiencyLedger::register_worker(WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (boot.valid()) live_workers_.insert(boot);
}
void EfficiencyLedger::unregister_worker(WorkerBootId boot) {
  std::lock_guard<std::mutex> lk(mutex_);
  live_workers_.erase(boot);
}
bool EfficiencyLedger::has_worker(WorkerBootId boot) const {
  std::lock_guard<std::mutex> lk(mutex_);
  return live_workers_.count(boot) != 0;
}

namespace {

bool is_consumption_class(EfficiencyClass c) noexcept {
  return c == EfficiencyClass::kUseful || c == EfficiencyClass::kNecessaryOverhead ||
         c == EfficiencyClass::kAvoidableWaste || c == EfficiencyClass::kUnknown;
}

bool is_authoritative_use(const LedgerEntry& e) noexcept {
  if (e.fences.publication.valid()) return true;
  switch (e.event) {
    case EventType::kCacheHit:
    case EventType::kStateReused:
    case EventType::kCheckpointRestored:
      return true;
    default:
      return false;
  }
}

// At-most-once lifecycle gate helper.
bool is_lifecycle_completion(const LedgerEntry& e) noexcept {
  switch (e.event) {
    case EventType::kTransferCompleted:
    case EventType::kReservationReleased:
    case EventType::kAllocationReleased:
    case EventType::kRecoveryCompleted:
    case EventType::kRecoveryFailed:
      return true;
    default:
      return false;
  }
}

}  // namespace

Status EfficiencyLedger::check_authority(const LedgerEntry& e) const {
  if (e.fences.epoch.valid() && e.fences.epoch != epoch_) {
    return Status(ErrorCode::kStaleAuthority, "stale coordinator epoch");
  }
  if (e.fences.ledger_generation.valid() && e.fences.ledger_generation != ledger_generation_) {
    return Status(ErrorCode::kStaleAuthority, "stale ledger generation");
  }
  if (e.fences.account_generation.valid() && e.fences.account_generation != account_generation_) {
    return Status(ErrorCode::kStaleAuthority, "stale account generation");
  }
  if (!replaying_ && e.fences.worker_boot.valid() &&
      live_workers_.count(e.fences.worker_boot) == 0) {
    return Status(ErrorCode::kStaleAuthority, "stale worker boot id");
  }

  if (e.has_attempt()) {
    const auto it = attempts_.find(e.attempt.id);
    if (it != attempts_.end()) {
      const AttemptState& st = it->second;
      if (st.generation.valid() && e.attempt.generation.valid() &&
          st.generation != e.attempt.generation) {
        return Status(ErrorCode::kStaleAuthority, "stale attempt generation");
      }
      if (st.outcome != AttemptOutcome::kPending) {
        if (e.has_outcome()) {
          return Status(ErrorCode::kStaleAuthority, "attempt already terminal");
        }
        const bool signal =
            e.event == EventType::kExecutionCompleted ||
            e.event == EventType::kOutputPublished ||
            e.event == EventType::kOutputRejected ||
            e.event == EventType::kWorkAccepted ||
            e.event == EventType::kWorkRejected ||
            e.event == EventType::kCommitAuthorized ||
            e.event == EventType::kCommitRejected;
        if (signal) {
          return Status(ErrorCode::kStaleAuthority, "stale completion after terminal attempt");
        }
      }
      if (e.fences.publication.valid() && st.last_publication.valid() &&
          e.fences.publication <= st.last_publication) {
        return Status(ErrorCode::kStaleAuthority, "stale/duplicate publication");
      }
    }
  }
  return Status::success();
}

namespace {

Status check_classification_consistency(const LedgerEntry& e) {
  const EfficiencyClass c = e.classification;
  if (c == EfficiencyClass::kAvoidedWork) {
    if (!e.usage.is_zero()) {
      return Status(ErrorCode::kInvalidInput, "avoided work must not consume physical");
    }
    if (!e.avoided_work) {
      return Status(ErrorCode::kInvalidInput, "avoided work requires avoided-work usage evidence");
    }
  } else if (c == EfficiencyClass::kStrandedCapacity) {
    if (!e.usage.is_zero()) {
      return Status(ErrorCode::kInvalidInput, "stranded capacity must not consume physical");
    }
    if (!e.stranded_capacity) {
      return Status(ErrorCode::kInvalidInput, "stranded capacity requires capacity evidence");
    }
  } else if (is_consumption_class(c)) {
    if (e.avoided_work || e.stranded_capacity) {
      return Status(ErrorCode::kInvalidInput,
                    "consumption class entries must not carry avoided/stranded usage");
    }
    if (c == EfficiencyClass::kUseful && !is_authoritative_use(e)) {
      return Status(ErrorCode::kNotAuthoritative,
                    "USEFUL classification requires authoritative publication or valid reuse");
    }
  }
  return Status::success();
}

void distribute_dim(std::uint64_t amount, const std::vector<std::uint64_t>& weights,
                    std::vector<std::uint64_t>& out, std::uint64_t& residual) {
  out.assign(weights.size(), 0);
  residual = 0;
  if (weights.empty() || amount == 0) {
    residual = amount;
    return;
  }
  std::uint64_t totalw = 0;
  for (const auto w : weights) {
    std::uint64_t tmp = 0;
    if (!checked_add(totalw, w, tmp)) { residual = amount; return; }
    totalw = tmp;
  }
  if (totalw == 0) { residual = amount; return; }

  std::uint64_t allocated = 0;
  std::vector<std::uint64_t> rems(weights.size(), 0);
  for (std::size_t i = 0; i < weights.size(); ++i) {
    std::uint64_t rem = 0;
    const std::uint64_t q = mul_div(amount, weights[i], totalw, rem);
    out[i] = q;
    rems[i] = rem;
    std::uint64_t tmp = 0;
    if (!checked_add(allocated, q, tmp)) { residual = amount; return; }
    allocated = tmp;
  }
  if (allocated < amount) {
    const std::uint64_t leftover = amount - allocated;
    std::vector<std::size_t> order(weights.size());
    std::iota(order.begin(), order.end(), static_cast<std::size_t>(0));
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      if (rems[a] != rems[b]) return rems[a] > rems[b];
      return a < b;
    });
    const std::size_t n = static_cast<std::size_t>(leftover);
    for (std::size_t k = 0; k < n && k < order.size(); ++k) {
      ++out[order[k]];
    }
  }
  residual = 0;
}

}  // namespace

detail::AttributionResult EfficiencyLedger::compute_attribution(const LedgerEntry& e) const {
  detail::AttributionResult result;

  std::vector<std::pair<AccountId, std::uint64_t>> participants;
  if (e.shared && !e.shared->participants.empty()) {
    participants.reserve(e.shared->participants.size());
    for (const auto& p : e.shared->participants) participants.emplace_back(p.account, p.weight);
  } else if (e.account.valid()) {
    participants.emplace_back(e.account, 1);
  }

  std::vector<std::uint64_t> weights;
  weights.reserve(participants.size());
  for (const auto& p : participants) weights.push_back(p.second);

  result.shares.resize(participants.size());
  for (std::size_t i = 0; i < participants.size(); ++i) result.shares[i].account = participants[i].first;

  DimTotals residual_physical{};
  DimTotals residual_avoided{};
  DimTotals residual_stranded{};

  auto distribute_into = [&](const ResourceUsage* src, DimTotals detail::AttributionShare::* field) {
    if (!src) return;
    for (std::size_t d = 0; d < kDimensionCount; ++d) {
      if (participants.empty()) {
        if (field == &detail::AttributionShare::physical) residual_physical[d] += src->data()[d];
        else if (field == &detail::AttributionShare::avoided) residual_avoided[d] += src->data()[d];
        else residual_stranded[d] += src->data()[d];
        continue;
      }
      std::vector<std::uint64_t> out;
      std::uint64_t resid = 0;
      distribute_dim(src->data()[d], weights, out, resid);
      for (std::size_t i = 0; i < participants.size(); ++i) {
        (result.shares[i].*field)[d] = out[i];
      }
      if (resid != 0) {
        if (field == &detail::AttributionShare::physical) residual_physical[d] += resid;
        else if (field == &detail::AttributionShare::avoided) residual_avoided[d] += resid;
        else residual_stranded[d] += resid;
      }
    }
  };

  distribute_into(&e.usage, &detail::AttributionShare::physical);
  distribute_into(e.avoided_work ? &*e.avoided_work : nullptr, &detail::AttributionShare::avoided);
  distribute_into(e.stranded_capacity ? &*e.stranded_capacity : nullptr, &detail::AttributionShare::stranded);

  result.residual = residual_physical;
  bool any = false;
  for (std::size_t d = 0; d < kDimensionCount; ++d) {
    if (residual_physical[d] || residual_avoided[d] || residual_stranded[d]) { any = true; break; }
  }
  if (any) {
    detail::AttributionShare res;
    res.account = AccountId(kResidualAccountValue);
    res.physical = residual_physical;
    res.avoided = residual_avoided;
    res.stranded = residual_stranded;
    result.shares.push_back(res);
  }
  return result;
}

Status EfficiencyLedger::apply_attribution(const LedgerEntry& e,
                                           const detail::AttributionResult& attr) {
  std::unordered_map<AccountId, detail::AccountAggregate> pending;
  for (const auto& sh : attr.shares) {
    auto& agg = pending[sh.account];
    for (std::size_t d = 0; d < kDimensionCount; ++d) {
      if (!checked_add(agg.physical[d], sh.physical[d], agg.physical[d]))
        return Status(ErrorCode::kOverflow, "physical overflow");
      if (!checked_add(agg.avoided[d], sh.avoided[d], agg.avoided[d]))
        return Status(ErrorCode::kOverflow, "avoided overflow");
      if (!checked_add(agg.stranded[d], sh.stranded[d], agg.stranded[d]))
        return Status(ErrorCode::kOverflow, "stranded overflow");
    }
    switch (e.classification) {
      case EfficiencyClass::kUseful:
        for (std::size_t d = 0; d < kDimensionCount; ++d)
          if (!checked_add(agg.useful[d], sh.physical[d], agg.useful[d]))
            return Status(ErrorCode::kOverflow, "useful overflow");
        break;
      case EfficiencyClass::kNecessaryOverhead:
        for (std::size_t d = 0; d < kDimensionCount; ++d)
          if (!checked_add(agg.overhead[d], sh.physical[d], agg.overhead[d]))
            return Status(ErrorCode::kOverflow, "overhead overflow");
        break;
      case EfficiencyClass::kAvoidableWaste:
        for (std::size_t d = 0; d < kDimensionCount; ++d)
          if (!checked_add(agg.waste[d], sh.physical[d], agg.waste[d]))
            return Status(ErrorCode::kOverflow, "waste overflow");
        break;
      case EfficiencyClass::kUnknown:
        for (std::size_t d = 0; d < kDimensionCount; ++d)
          if (!checked_add(agg.unknown[d], sh.physical[d], agg.unknown[d]))
            return Status(ErrorCode::kOverflow, "unknown overflow");
        break;
      case EfficiencyClass::kAvoidedWork:
      case EfficiencyClass::kStrandedCapacity:
        break;
    }
  }

  for (auto& [acc, agg] : pending) {
    auto it = accounts_.find(acc);
    if (it == accounts_.end()) {
      accounts_.emplace(acc, agg);
    } else {
      auto& dst = it->second;
      for (std::size_t d = 0; d < kDimensionCount; ++d) {
        if (!checked_add(dst.physical[d], agg.physical[d], dst.physical[d]))
          return Status(ErrorCode::kOverflow, "physical overflow");
        if (!checked_add(dst.avoided[d], agg.avoided[d], dst.avoided[d]))
          return Status(ErrorCode::kOverflow, "avoided overflow");
        if (!checked_add(dst.stranded[d], agg.stranded[d], dst.stranded[d]))
          return Status(ErrorCode::kOverflow, "stranded overflow");
        if (!checked_add(dst.useful[d], agg.useful[d], dst.useful[d]))
          return Status(ErrorCode::kOverflow, "useful overflow");
        if (!checked_add(dst.overhead[d], agg.overhead[d], dst.overhead[d]))
          return Status(ErrorCode::kOverflow, "overhead overflow");
        if (!checked_add(dst.waste[d], agg.waste[d], dst.waste[d]))
          return Status(ErrorCode::kOverflow, "waste overflow");
        if (!checked_add(dst.unknown[d], agg.unknown[d], dst.unknown[d]))
          return Status(ErrorCode::kOverflow, "unknown overflow");
      }
    }
  }
  return Status::success();
}

Status EfficiencyLedger::apply_reclassify_locked(const LedgerEntry& revision) {
  if (!revision.revision_target) {
    return Status(ErrorCode::kInvalidInput, "revision event missing target");
  }
  const auto tit = index_.find(*revision.revision_target);
  if (tit == index_.end()) {
    return Status(ErrorCode::kInvalidInput, "revision target entry does not exist");
  }
  const LedgerEntry& target = history_[tit->second];
  const EfficiencyClass old_cls = target.classification;
  const EfficiencyClass new_cls = revision.classification;

  const bool old_cons = is_consumption_class(old_cls);
  const bool new_cons = is_consumption_class(new_cls);
  if (old_cons != new_cons) {
    return Status(ErrorCode::kInvalidInput,
                  "cannot reclassify across consumption / non-consumption classes");
  }
  if (old_cons) {
    const detail::AttributionResult attr = compute_attribution(target);
    for (const auto& sh : attr.shares) {
      auto it = accounts_.find(sh.account);
      if (it == accounts_.end()) continue;
      auto& agg = it->second;
      auto move_out = [&](DimTotals& bucket) -> bool {
        for (std::size_t d = 0; d < kDimensionCount; ++d) {
          std::uint64_t nv = 0;
          if (!checked_sub(bucket[d], sh.physical[d], nv)) return false;
          bucket[d] = nv;
        }
        return true;
      };
      auto move_in = [&](DimTotals& bucket) -> bool {
        for (std::size_t d = 0; d < kDimensionCount; ++d) {
          std::uint64_t nv = 0;
          if (!checked_add(bucket[d], sh.physical[d], nv)) return false;
          bucket[d] = nv;
        }
        return true;
      };
      bool ok = true;
      switch (old_cls) {
        case EfficiencyClass::kUseful: ok = move_out(agg.useful); break;
        case EfficiencyClass::kNecessaryOverhead: ok = move_out(agg.overhead); break;
        case EfficiencyClass::kAvoidableWaste: ok = move_out(agg.waste); break;
        case EfficiencyClass::kUnknown: ok = move_out(agg.unknown); break;
        default: ok = false; break;
      }
      if (!ok) return Status(ErrorCode::kReconciliationFailure, "reclassify underflow");
      switch (new_cls) {
        case EfficiencyClass::kUseful: ok = move_in(agg.useful); break;
        case EfficiencyClass::kNecessaryOverhead: ok = move_in(agg.overhead); break;
        case EfficiencyClass::kAvoidableWaste: ok = move_in(agg.waste); break;
        case EfficiencyClass::kUnknown: ok = move_in(agg.unknown); break;
        default: ok = false; break;
      }
      if (!ok) return Status(ErrorCode::kReconciliationFailure, "reclassify overflow");
    }
  }
  history_.push_back(revision);
  index_[revision.id] = history_.size() - 1;
  return Status::success();
}

Status EfficiencyLedger::append_locked(const LedgerEntry& e) {
  if (shutting_down_) return Status(ErrorCode::kShuttingDown, "ledger is shutting down");
  if (!e.validate()) return Status(ErrorCode::kInvalidInput, "invalid accounting entry");

  if (e.event == EventType::kReclassify) {
    if (index_.count(e.id)) return Status(ErrorCode::kDuplicateEntry, "duplicate revision entry");
    if (!e.usage.is_zero()) {
      return Status(ErrorCode::kInvalidInput, "revision event must not consume");
    }
    Status st = check_authority(e);
    if (!st.ok()) return st;
    return apply_reclassify_locked(e);
  }

  if (index_.count(e.id)) return Status(ErrorCode::kDuplicateEntry, "duplicate entry id");

  Status st = check_classification_consistency(e);
  if (!st.ok()) return st;

  st = check_authority(e);
  if (!st.ok()) return st;

  // At-most-once lifecycle gates (duplicate completion must not double-count).
  if (is_lifecycle_completion(e)) {
    if (e.event == EventType::kTransferCompleted && e.owners.transfer.valid() &&
        transfer_completed_.count(e.owners.transfer)) {
      return Status(ErrorCode::kDuplicateEntry, "duplicate transfer completion");
    }
    if (e.event == EventType::kReservationReleased && e.owners.reservation.valid() &&
        reservation_released_.count(e.owners.reservation)) {
      return Status(ErrorCode::kDuplicateEntry, "duplicate reservation release");
    }
    if (e.event == EventType::kAllocationReleased && e.owners.allocation.valid() &&
        allocation_released_.count(e.owners.allocation)) {
      return Status(ErrorCode::kDuplicateEntry, "duplicate allocation release");
    }
    if ((e.event == EventType::kRecoveryCompleted || e.event == EventType::kRecoveryFailed) &&
        e.owners.recovery.valid() && recovery_completed_.count(e.owners.recovery)) {
      return Status(ErrorCode::kDuplicateEntry, "duplicate recovery completion");
    }
  }

  const detail::AttributionResult attr = compute_attribution(e);
  if (attr.invalid) return Status(ErrorCode::kInvalidInput, "unable to attribute entry");

  st = apply_attribution(e, attr);
  if (!st.ok()) return st;

  if (is_lifecycle_completion(e)) {
    if (e.event == EventType::kTransferCompleted && e.owners.transfer.valid())
      transfer_completed_.insert(e.owners.transfer);
    if (e.event == EventType::kReservationReleased && e.owners.reservation.valid())
      reservation_released_.insert(e.owners.reservation);
    if (e.event == EventType::kAllocationReleased && e.owners.allocation.valid())
      allocation_released_.insert(e.owners.allocation);
    if ((e.event == EventType::kRecoveryCompleted || e.event == EventType::kRecoveryFailed) &&
        e.owners.recovery.valid())
      recovery_completed_.insert(e.owners.recovery);
  }

  if (e.has_attempt()) {
    auto& as = attempts_[e.attempt.id];
    if (e.has_outcome()) {
      as.outcome = e.attempt_outcome;
      as.generation = e.attempt.generation;
      as.authoritative = true;
    }
    if (e.fences.publication.valid() &&
        (!as.last_publication.valid() || e.fences.publication > as.last_publication)) {
      as.last_publication = e.fences.publication;
    }
  }

  history_.push_back(e);
  index_[e.id] = history_.size() - 1;
  return Status::success();
}

Status EfficiencyLedger::append(const LedgerEntry& entry) {
  std::lock_guard<std::mutex> lk(mutex_);
  return append_locked(entry);
}

Status EfficiencyLedger::append(const std::vector<LedgerEntry>& entries) {
  std::lock_guard<std::mutex> lk(mutex_);
  for (const auto& e : entries) {
    Status st = append_locked(e);
    if (!st.ok()) return st;
  }
  return Status::success();
}

Status EfficiencyLedger::reclassify(const ReclassifyRequest& req) {
  LedgerEntry rev;
  rev.id = req.revision_id;
  rev.event = EventType::kReclassify;
  rev.classification = req.new_classification;
  rev.subcategory = req.new_subcategory;
  rev.provenance = req.provenance;
  rev.fences = req.fences;
  rev.revision_target = req.target;
  rev.note = req.note;
  {
    const auto tit = index_.find(req.target);
    if (tit != index_.end()) {
      const auto& t = history_[tit->second];
      rev.account = t.account.valid() ? t.account : AccountId(kResidualAccountValue);
      rev.attempt = t.attempt;
    }
  }
  std::lock_guard<std::mutex> lk(mutex_);
  return append_locked(rev);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool EfficiencyLedger::contains(AccountingEntryId id) const {
  std::lock_guard<std::mutex> lk(mutex_);
  return index_.count(id) != 0;
}
const LedgerEntry* EfficiencyLedger::get(AccountingEntryId id) const {
  std::lock_guard<std::mutex> lk(mutex_);
  const auto it = index_.find(id);
  if (it == index_.end()) return nullptr;
  return &history_[it->second];
}
std::size_t EfficiencyLedger::entry_count() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return history_.size();
}
std::vector<LedgerEntry> EfficiencyLedger::history() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return history_;
}
std::vector<AccountSummary> EfficiencyLedger::account_summaries() const {
  std::lock_guard<std::mutex> lk(mutex_);
  std::vector<AccountSummary> out;
  out.reserve(accounts_.size());
  for (const auto& [acc, agg] : accounts_) {
    AccountSummary s;
    s.account = acc;
    s.physical = agg.physical;
    s.useful = agg.useful;
    s.overhead = agg.overhead;
    s.waste = agg.waste;
    s.unknown = agg.unknown;
    s.avoided = agg.avoided;
    s.stranded = agg.stranded;
    out.push_back(s);
  }
  std::sort(out.begin(), out.end(), [](const AccountSummary& a, const AccountSummary& b) {
    return a.account.value() < b.account.value();
  });
  return out;
}
AccountSummary EfficiencyLedger::account_summary(AccountId account) const {
  std::lock_guard<std::mutex> lk(mutex_);
  AccountSummary s;
  s.account = account;
  const auto it = accounts_.find(account);
  if (it != accounts_.end()) {
    const auto& agg = it->second;
    s.physical = agg.physical;
    s.useful = agg.useful;
    s.overhead = agg.overhead;
    s.waste = agg.waste;
    s.unknown = agg.unknown;
    s.avoided = agg.avoided;
    s.stranded = agg.stranded;
  }
  return s;
}
bool EfficiencyLedger::has_account(AccountId account) const {
  std::lock_guard<std::mutex> lk(mutex_);
  return accounts_.count(account) != 0;
}

// ---------------------------------------------------------------------------
// Reconciliation / digest / replay
// ---------------------------------------------------------------------------

Reconciliation EfficiencyLedger::reconcile() const {
  std::lock_guard<std::mutex> lk(mutex_);
  Reconciliation r;
  r.per_dimension_closed.assign(kDimensionCount, true);
  r.entry_count = history_.size();

  r.physical = {};
  r.classified = {};
  r.avoided = {};
  r.stranded = {};
  for (const auto& [acc, agg] : accounts_) {
    (void)acc;
    for (std::size_t d = 0; d < kDimensionCount; ++d) {
      r.physical[d] += agg.physical[d];
      r.classified[d] += agg.useful[d] + agg.overhead[d] + agg.waste[d] + agg.unknown[d];
      r.avoided[d] += agg.avoided[d];
      r.stranded[d] += agg.stranded[d];
    }
  }

  r.closed = true;
  for (std::size_t d = 0; d < kDimensionCount; ++d) {
    if (r.physical[d] != r.classified[d]) {
      r.per_dimension_closed[d] = false;
      r.closed = false;
    }
    r.unclassified[d] = r.physical[d] - r.classified[d];
  }

  for (const auto& [acc, agg] : accounts_) {
    (void)acc;
    for (std::size_t d = 0; d < kDimensionCount; ++d) {
      const std::uint64_t cls = agg.useful[d] + agg.overhead[d] + agg.waste[d] + agg.unknown[d];
      if (cls != agg.physical[d]) {
        r.closed = false;
        r.per_dimension_closed[d] = false;
        r.messages.push_back("dimension " +
                             std::string(to_string(static_cast<ResourceDimension>(d))) +
                             " does not close for an account");
      }
    }
  }
  if (r.closed && r.messages.empty()) r.messages.push_back("reconciliation closed");
  return r;
}

Digest EfficiencyLedger::digest() const {
  std::lock_guard<std::mutex> lk(mutex_);
  HashAccumulator h;
  h.mix_bytes(reinterpret_cast<const std::uint8_t*>(kMagic), 4);
  h.mix64(kPersistenceFormatVersion);
  h.mix64(static_cast<std::uint64_t>(history_.size()));
  std::string encoded;
  for (const auto& e : history_) {
    encoded.clear();
    encode_entry(e, encoded);
    h.mix_bytes(reinterpret_cast<const std::uint8_t*>(encoded.data()), encoded.size());
  }
  return h.digest();
}

Status EfficiencyLedger::load_history(const std::vector<LedgerEntry>& entries) {
  std::lock_guard<std::mutex> lk(mutex_);
  history_.clear();
  index_.clear();
  accounts_.clear();
  attempts_.clear();
  transfer_completed_.clear();
  reservation_released_.clear();
  allocation_released_.clear();
  recovery_completed_.clear();

  replaying_ = true;
  for (const auto& e : entries) {
    Status st = append_locked(e);
    if (!st.ok()) {
      replaying_ = false;
      return Status(st.code(), "replay failed: " + st.message());
    }
  }
  replaying_ = false;
  return Status::success();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

Status EfficiencyLedger::save_to_string(std::string& out) const {
  std::lock_guard<std::mutex> lk(mutex_);
  out.clear();
  out.reserve(kHeaderLen + history_.size() * 128 + kFooterLen);
  out.append(kMagic, 4);
  put_u32(out, kPersistenceFormatVersion);
  put_u64(out, ledger_id_.value());
  put_u64(out, epoch_.value());
  put_u64(out, ledger_generation_.value());
  put_u64(out, account_generation_.value());
  put_u64(out, static_cast<std::uint64_t>(history_.size()));
  for (const auto& e : history_) encode_entry(e, out);
  const std::uint32_t crc = crc32c(out.data(), out.size());
  put_u32(out, crc);
  return Status::success();
}

Status EfficiencyLedger::save(const std::filesystem::path& path) const {
  std::string bytes;
  Status st = save_to_string(bytes);
  if (!st.ok()) return st;
  const std::filesystem::path tmp = path.string() + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return Status(ErrorCode::kPersistenceCorrupt, "cannot open file for write");
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!f) return Status(ErrorCode::kPersistenceCorrupt, "write failed");
  }
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::error_code rme;
    std::filesystem::remove(tmp, rme);
    return Status(ErrorCode::kPersistenceCorrupt, "atomic replace failed: " + ec.message());
  }
  return Status::success();
}

Status EfficiencyLedger::load_from_string(const std::string& bytes, std::string* err) {
  if (bytes.size() < kHeaderLen + kFooterLen) {
    if (err) *err = "truncated";
    return Status(ErrorCode::kPersistenceCorrupt, "truncated persistence record");
  }
  const std::size_t total = bytes.size();
  const std::size_t payload_end = total - kFooterLen;
  const std::uint32_t stored_crc =
      static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[payload_end])) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[payload_end + 1])) << 8) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[payload_end + 2])) << 16) |
      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[payload_end + 3])) << 24);
  const std::uint32_t calc_crc = crc32c(bytes.data(), payload_end);
  if (stored_crc != calc_crc) {
    if (err) *err = "checksum mismatch";
    return Status(ErrorCode::kPersistenceCorrupt, "CRC-32C mismatch");
  }
  if (std::memcmp(bytes.data(), kMagic, 4) != 0) {
    if (err) *err = "bad magic";
    return Status(ErrorCode::kPersistenceCorrupt, "bad magic");
  }
  std::size_t pos = 4;
  auto read_u32 = [&](std::uint32_t& out) -> bool {
    if (pos + 4 > payload_end) return false;
    out = static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos])) |
          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 1])) << 8) |
          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 2])) << 16) |
          (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[pos + 3])) << 24);
    pos += 4;
    return true;
  };
  auto read_u64 = [&](std::uint64_t& out) -> bool {
    if (pos + 8 > payload_end) return false;
    out = 0;
    for (int i = 0; i < 8; ++i) {
      out |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes[pos + i])) << (8 * i);
    }
    pos += 8;
    return true;
  };

  std::uint32_t version = 0;
  std::uint64_t ledger_id_v = 0, epoch_v = 0, ledger_gen_v = 0, account_gen_v = 0, count_v = 0;
  if (!read_u32(version)) {
    if (err) *err = "read version";
    return Status(ErrorCode::kPersistenceCorrupt, "truncated");
  }
  if (version != kPersistenceFormatVersion) {
    if (err) *err = "unknown version";
    return Status(ErrorCode::kPersistenceCorrupt, "unknown persistence format version");
  }
  if (!read_u64(ledger_id_v)) return Status(ErrorCode::kPersistenceCorrupt, "truncated");
  if (!read_u64(epoch_v)) return Status(ErrorCode::kPersistenceCorrupt, "truncated");
  if (!read_u64(ledger_gen_v)) return Status(ErrorCode::kPersistenceCorrupt, "truncated");
  if (!read_u64(account_gen_v)) return Status(ErrorCode::kPersistenceCorrupt, "truncated");
  if (!read_u64(count_v)) return Status(ErrorCode::kPersistenceCorrupt, "truncated");
  if (count_v > (1ull << 28)) return Status(ErrorCode::kPersistenceCorrupt, "absurd entry count");

  std::vector<LedgerEntry> entries;
  entries.reserve(static_cast<std::size_t>(count_v));
  for (std::uint64_t i = 0; i < count_v; ++i) {
    LedgerEntry e;
    if (!decode_entry(bytes, pos, e)) {
      if (err) *err = "decode entry";
      return Status(ErrorCode::kPersistenceCorrupt, "corrupt entry at index " + std::to_string(i));
    }
    entries.push_back(e);
  }
  if (pos != payload_end) {
    if (err) *err = "trailing garbage";
    return Status(ErrorCode::kPersistenceCorrupt, "trailing garbage after entries");
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    ledger_id_ = LedgerId(ledger_id_v);
    epoch_ = CoordinatorEpoch(epoch_v);
    ledger_generation_ = LedgerGeneration(ledger_gen_v);
    account_generation_ = AccountGeneration(account_gen_v);
    shutting_down_ = false;
    epoch_advanced_ = false;
    history_.clear();
    index_.clear();
    accounts_.clear();
    attempts_.clear();
    transfer_completed_.clear();
    reservation_released_.clear();
    allocation_released_.clear();
    recovery_completed_.clear();
  }
  Status st = load_history(entries);
  if (!st.ok()) {
    if (err) *err = "replay: " + st.message();
    return Status(ErrorCode::kPersistenceCorrupt, "replay rejected loaded history: " + st.message());
  }
  return Status::success();
}

Status EfficiencyLedger::load(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return Status(ErrorCode::kPersistenceCorrupt, "cannot open file for read");
  std::ostringstream ss;
  ss << f.rdbuf();
  if (!f.good() && !f.eof()) return Status(ErrorCode::kPersistenceCorrupt, "read failed");
  return load_from_string(ss.str());
}

void EfficiencyLedger::set_shutting_down() {
  std::lock_guard<std::mutex> lk(mutex_);
  shutting_down_ = true;
}
bool EfficiencyLedger::shutting_down() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return shutting_down_;
}

// ---------------------------------------------------------------------------
// Canonical entry codec (public)
// ---------------------------------------------------------------------------

void encode_entry_bytes(const LedgerEntry& e, std::string& out) { encode_entry(e, out); }

bool decode_entry_bytes(const std::string& in, std::size_t& offset, LedgerEntry& e) {
  return decode_entry(in, offset, e);
}

}  // namespace el
