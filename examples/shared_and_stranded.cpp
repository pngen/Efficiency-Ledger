#include <cstdio>

#include "efficiency_ledger/model.hpp"

using namespace el;

int main() {
  EfficiencyLedger led;
  const AccountId a(1), b(2), c(3), d(4);

  // Shared batch kernel attributes exactly across three accounts.
  LedgerEntry shared;
  shared.id = AccountingEntryId(1);
  shared.event = EventType::kWorkAccepted;
  shared.classification = EfficiencyClass::kUseful;
  shared.usage.set(ResourceDimension::kAcceleratorNanoseconds, 100);
  shared.fences.publication = PublicationId(1);
  SharedRef sh;
  sh.policy = AttributionPolicy::kEqualShare;
  sh.participants = {{a, 1}, {b, 1}, {c, 1}};
  shared.shared = sh;
  led.append(shared);

  for (AccountId ac : {a, b, c}) {
    auto s = led.account_summary(ac);
    std::printf("account %llu useful=%llu\n", (unsigned long long)ac.value(),
                (unsigned long long)s.useful[0]);
  }

  // Fragmentation strands capacity that physically exists but cannot serve work.
  LedgerEntry stranded;
  stranded.id = AccountingEntryId(2);
  stranded.event = EventType::kCapacityStranded;
  stranded.classification = EfficiencyClass::kStrandedCapacity;
  stranded.stranded_capacity = ResourceUsage(ResourceDimension::kAcceleratorMemoryByteNanoseconds, 4096);
  stranded.account = d;
  stranded.subcategory.stranded = StrandedReason::kFragmentation;
  led.append(stranded);

  auto sd = led.account_summary(d);
  std::printf("stranded account %llu stranded(bytes)=%llu\n", (unsigned long long)d.value(),
              (unsigned long long)sd.stranded[static_cast<std::size_t>(ResourceDimension::kAcceleratorMemoryByteNanoseconds)]);
  std::printf("reconcile=%s\n", led.reconcile().closed ? "closed" : "OPEN");
  return 0;
}
