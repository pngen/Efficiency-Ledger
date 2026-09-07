#include <cstdio>

#include "efficiency_ledger/model.hpp"

using namespace el;

int main() {
  EfficiencyLedger led;
  const AccountId acc(7);

  // Three attempts: two fail, the last succeeds and publishes.
  auto mk = [&](std::uint64_t id, std::uint64_t ns, AttemptId att, AttemptOutcome out,
                EfficiencyClass cls, EventType ev, std::uint64_t pub = 0) {
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
  };

  led.append(mk(1, 300, AttemptId(101), AttemptOutcome::kFailed, EfficiencyClass::kAvoidableWaste,
                EventType::kAttemptFailed));
  led.append(mk(2, 250, AttemptId(102), AttemptOutcome::kFailed, EfficiencyClass::kAvoidableWaste,
                EventType::kAttemptFailed));
  led.append(mk(3, 200, AttemptId(103), AttemptOutcome::kSucceeded, EfficiencyClass::kUseful,
                EventType::kOutputPublished, 1));

  auto s = led.account_summary(acc);
  std::printf("retries: physical=%llu useful=%llu waste=%llu\n", (unsigned long long)s.physical[0],
              (unsigned long long)s.useful[0], (unsigned long long)s.waste[0]);

  // A cache hit avoids 500 ns of recomputation (counterfactual, no physical).
  LedgerEntry avoid;
  avoid.id = AccountingEntryId(4);
  avoid.event = EventType::kCacheHit;
  avoid.classification = EfficiencyClass::kAvoidedWork;
  avoid.avoided_work = ResourceUsage(ResourceDimension::kAcceleratorNanoseconds, 500);
  avoid.account = acc;
  avoid.subcategory.avoided = AvoidedWorkReason::kCacheHit;
  led.append(avoid);

  s = led.account_summary(acc);
  std::printf("after reuse: physical=%llu avoided=%llu\n", (unsigned long long)s.physical[0],
              (unsigned long long)s.avoided[0]);
  std::printf("reconcile=%s\n", led.reconcile().closed ? "closed" : "OPEN");
  return 0;
}
