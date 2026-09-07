#include <cstdio>

#include "efficiency_ledger/model.hpp"

using namespace el;

int main() {
  EfficiencyLedger led;
  const AccountId acc(42);

  auto mk = [&](std::uint64_t id, EventType ev, EfficiencyClass cls, std::uint64_t ns,
                AttemptId att, AttemptOutcome out, std::uint64_t pub = 0) {
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

  led.append(mk(1, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 300,
                AttemptId(11), AttemptOutcome::kFailed));
  led.append(mk(2, EventType::kOutputPublished, EfficiencyClass::kUseful, 200,
                AttemptId(12), AttemptOutcome::kSucceeded, 7));

  auto s = led.account_summary(acc);
  std::printf("physical=%llu useful=%llu waste=%llu overhead=%llu unknown=%llu\n",
              (unsigned long long)s.physical[0], (unsigned long long)s.useful[0],
              (unsigned long long)s.waste[0], (unsigned long long)s.overhead[0],
              (unsigned long long)s.unknown[0]);
  auto r = led.reconcile();
  std::printf("reconcile=%s digest=%s\n", r.closed ? "closed" : "OPEN",
              led.digest().hex().c_str());
  return 0;
}
