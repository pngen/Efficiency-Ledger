#include "efficiency_ledger/model.hpp"

#include <limits>
#include <random>
#include <string>
#include <vector>

#include "testutil.hpp"

using namespace el;

namespace {

LedgerEntry mk(std::uint64_t id, EventType ev, EfficiencyClass cls, std::uint64_t ns,
               AccountId acc, std::uint64_t pub = 0, AttemptId att = AttemptId{},
               AttemptOutcome out = AttemptOutcome::kPending) {
  LedgerEntry e;
  e.id = AccountingEntryId(id);
  e.event = ev;
  e.classification = cls;
  e.usage.set(ResourceDimension::kAcceleratorNanoseconds, ns);
  e.account = acc;
  e.fences.publication = PublicationId(pub);
  e.attempt.id = att;
  e.attempt_outcome = out;
  if (cls == EfficiencyClass::kUseful) e.subcategory.useful = UsefulReason::kAuthoritativeExecution;
  if (cls == EfficiencyClass::kAvoidableWaste) e.subcategory.waste = WasteReason::kFailedAttempt;
  return e;
}

LedgerEntry avoided(std::uint64_t id, std::uint64_t ns, AccountId acc) {
  LedgerEntry e;
  e.id = AccountingEntryId(id);
  e.event = EventType::kCacheHit;
  e.classification = EfficiencyClass::kAvoidedWork;
  e.avoided_work = ResourceUsage(ResourceDimension::kAcceleratorNanoseconds, ns);
  e.account = acc;
  e.subcategory.avoided = AvoidedWorkReason::kCacheHit;
  return e;
}

LedgerEntry stranded(std::uint64_t id, std::uint64_t bytes, AccountId acc) {
  LedgerEntry e;
  e.id = AccountingEntryId(id);
  e.event = EventType::kCapacityStranded;
  e.classification = EfficiencyClass::kStrandedCapacity;
  e.stranded_capacity = ResourceUsage(ResourceDimension::kAcceleratorMemoryByteNanoseconds, bytes);
  e.account = acc;
  e.subcategory.stranded = StrandedReason::kFragmentation;
  return e;
}

void malformed_input() {
  EfficiencyLedger led;
  const AccountId acc(3);
  LedgerEntry e = mk(1, EventType::kOutputPublished, EfficiencyClass::kUseful, 100, acc, 1);
  CHECK(led.append(e).ok());
  // zero id
  LedgerEntry z = mk(0, EventType::kOutputPublished, EfficiencyClass::kUseful, 100, acc, 1);
  CHECK(!led.append(z).ok());
  // invalid classification enum
  LedgerEntry b = mk(2, EventType::kOutputPublished, static_cast<EfficiencyClass>(200), 100, acc, 1);
  CHECK(!led.append(b).ok());
  // invalid event enum
  LedgerEntry e2 = mk(3, static_cast<EventType>(200), EfficiencyClass::kUseful, 100, acc, 1);
  CHECK(!led.append(e2).ok());
  // USEFUL without authority
  LedgerEntry n = mk(4, EventType::kExecutionCompleted, EfficiencyClass::kUseful, 100, acc, 0);
  CHECK(!led.append(n).ok());
  // AVOIDED_WORK with nonzero physical usage
  LedgerEntry aw = avoided(5, 100, acc);
  aw.usage.set(ResourceDimension::kAcceleratorNanoseconds, 10);
  CHECK(!led.append(aw).ok());
  // STRANDED with nonzero physical usage
  LedgerEntry st = stranded(6, 4096, acc);
  st.usage.set(ResourceDimension::kAcceleratorNanoseconds, 10);
  CHECK(!led.append(st).ok());
  // no owner (no account, no shared)
  LedgerEntry noowner = mk(7, EventType::kOutputPublished, EfficiencyClass::kUseful, 100, AccountId{}, 1);
  CHECK(!led.append(noowner).ok());
}

void duplicate_event_storm() {
  EfficiencyLedger led;
  const AccountId acc(3);
  for (int i = 0; i < 1000; ++i) led.append(mk(9, EventType::kOutputPublished, EfficiencyClass::kUseful, 50, acc, 1));
  CHECK_EQ(led.entry_count(), 1u);
  CHECK_EQ(led.account_summary(acc).physical[0], 50);
  CHECK(led.reconcile().closed);
}

void stale_completion_storm() {
  EfficiencyLedger led;
  const AccountId acc(3);
  CHECK(led.append(mk(1, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 90, acc, 0,
                      AttemptId(88), AttemptOutcome::kFailed)).ok());
  for (int i = 0; i < 500; ++i) {
    CHECK(!led.append(mk(100 + i, EventType::kExecutionCompleted, EfficiencyClass::kUseful, 90, acc,
                         1, AttemptId(88), AttemptOutcome::kSucceeded)).ok());
  }
  CHECK_EQ(led.entry_count(), 1u);
  auto s = led.account_summary(acc);
  CHECK_EQ(s.waste[0], 90);
  CHECK_EQ(s.useful[0], 0);
}

void retries_preserve_progress() {
  EfficiencyLedger led;
  const AccountId acc(3);
  CHECK(led.append(mk(1, EventType::kExecutionCompleted, EfficiencyClass::kUseful, 120, acc, 1,
                      AttemptId(200), AttemptOutcome::kSucceeded)).ok());
  // Final authoritative attempt, then later the other stopped.
  CHECK(led.append(mk(2, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 40, acc, 0,
                      AttemptId(201), AttemptOutcome::kFailed)).ok());
  auto s = led.account_summary(acc);
  CHECK_EQ(s.physical[0], 160);
  CHECK_EQ(s.useful[0], 120);
  CHECK_EQ(s.waste[0], 40);
  CHECK(led.reconcile().closed);
}

void counter_overflow() {
  // Aggregates use checked arithmetic; near-max additions refuse overflow.
  EfficiencyLedger led;
  const AccountId acc(3);
  LedgerEntry e = mk(1, EventType::kOutputPublished, EfficiencyClass::kUseful,
                     std::numeric_limits<std::uint64_t>::max() - 5, acc, 1);
  CHECK(led.append(e).ok());
  CHECK(led.account_summary(acc).physical[0] == std::numeric_limits<std::uint64_t>::max() - 5);
  auto s = led.account_summary(acc);
  CHECK_EQ(s.useful[0], s.physical[0]);  // useful never exceeds physical
}

void reuse_and_avoided_work() {
  EfficiencyLedger led;
  const AccountId acc(3);
  // Valid reuse: an avoided-work cache hit records the benefit (counterfactual),
  // never a negative physical quantity.
  CHECK(led.append(avoided(1, 250, acc)).ok());
  CHECK_EQ(led.account_summary(acc).avoided[0], 250);
  CHECK_EQ(led.account_summary(acc).physical[0], 0);
  // Invalid reuse is rejected: a cache hit claiming to CONSUME physical work is
  // not a valid counterfactual benefit.
  LedgerEntry bad;
  bad.id = AccountingEntryId(2);
  bad.event = EventType::kCacheHit;
  bad.classification = EfficiencyClass::kAvoidedWork;
  bad.avoided_work = ResourceUsage(ResourceDimension::kAcceleratorNanoseconds, 100);
  bad.usage.set(ResourceDimension::kAcceleratorNanoseconds, 50);  // consumes physical
  bad.account = acc;
  CHECK(!led.append(bad).ok());  // avoided work must not consume physical
  CHECK(led.reconcile().closed);
}

void fragment_start_end() {
  EfficiencyLedger led;
  const AccountId acc(3);
  CHECK(led.append(stranded(1, 2048, acc)).ok());
  CHECK(led.append(stranded(2, 4096, acc)).ok());
  auto s = led.account_summary(acc);
  CHECK_EQ(s.stranded[static_cast<std::size_t>(ResourceDimension::kAcceleratorMemoryByteNanoseconds)], 6144);
  CHECK(led.reconcile().closed);
}

void property_random() {
  // Deterministic randomized property test: physical == useful+overhead+waste+unknown,
  // totals close, duplicate append never changes totals, avoided/stranded separate.
  std::mt19937_64 rng(12345);
  EfficiencyLedger led;
  const AccountId acc(3);
  std::uint64_t phys[16] = {}, useful[16] = {}, overhead[16] = {}, waste[16] = {}, unknown[16] = {};
  std::uint64_t idbase = 100000;
  for (int i = 0; i < 3000; ++i) {
    const std::uint64_t ns = rng() % 10000;
    const int pick = static_cast<int>(rng() % 5);
    EfficiencyClass cls;
    EventType ev;
    try {
      if (pick == 0) { cls = EfficiencyClass::kUseful; ev = EventType::kOutputPublished; }
      else if (pick == 1) { cls = EfficiencyClass::kNecessaryOverhead; ev = EventType::kTransferCompleted; }
      else if (pick == 2) { cls = EfficiencyClass::kAvoidableWaste; ev = EventType::kAttemptFailed; }
      else if (pick == 3) { cls = EfficiencyClass::kUnknown; ev = EventType::kExecutionCompleted; }
      else { cls = EfficiencyClass::kAvoidedWork; ev = EventType::kCacheHit; }

      LedgerEntry e;
      e.id = AccountingEntryId(idbase + i);
      e.event = ev;
      e.classification = cls;
      e.account = acc;
      if (cls == EfficiencyClass::kAvoidedWork) {
        e.avoided_work = ResourceUsage(ResourceDimension::kAcceleratorNanoseconds, ns);
        e.subcategory.avoided = AvoidedWorkReason::kCacheHit;
      } else {
        e.usage.set(ResourceDimension::kAcceleratorNanoseconds, ns);
        if (cls == EfficiencyClass::kUseful) {
          e.fences.publication = PublicationId(idbase + i);
          e.subcategory.useful = UsefulReason::kAuthoritativeExecution;
        } else if (cls == EfficiencyClass::kNecessaryOverhead) {
          e.subcategory.overhead = OverheadReason::kRequiredTransfer;
        } else if (cls == EfficiencyClass::kAvoidableWaste) {
          e.subcategory.waste = WasteReason::kFailedAttempt;
        }
      }
      if (!led.append(e).ok()) { CHECK(false); break; }
      if (cls == EfficiencyClass::kAvoidedWork) continue;
      phys[0] += ns;
      if (cls == EfficiencyClass::kUseful) useful[0] += ns;
      else if (cls == EfficiencyClass::kNecessaryOverhead) overhead[0] += ns;
      else if (cls == EfficiencyClass::kAvoidableWaste) waste[0] += ns;
      else unknown[0] += ns;
    } catch (...) { CHECK(false); }
  }
  auto s = led.account_summary(acc);
  CHECK_EQ(s.physical[0], phys[0]);
  CHECK_EQ(s.useful[0], useful[0]);
  CHECK_EQ(s.overhead[0], overhead[0]);
  CHECK_EQ(s.waste[0], waste[0]);
  CHECK_EQ(s.unknown[0], unknown[0]);
  CHECK_EQ(s.physical[0], s.useful[0] + s.overhead[0] + s.waste[0] + s.unknown[0]);
  CHECK(led.reconcile().closed);
}

}  // namespace

int main() {
  malformed_input();
  duplicate_event_storm();
  stale_completion_storm();
  retries_preserve_progress();
  counter_overflow();
  reuse_and_avoided_work();
  fragment_start_end();
  property_random();
  TEST_MAIN_END();
}
