#include <cstdio>
#include <filesystem>

#include "efficiency_ledger/model.hpp"

using namespace el;

int main() {
  const std::filesystem::path path = "example_ledger.led";
  const AccountId acc(9);
  {
    EfficiencyLedger led;
    LedgerEntry e;
    e.id = AccountingEntryId(1);
    e.event = EventType::kOutputPublished;
    e.classification = EfficiencyClass::kUseful;
    e.usage.set(ResourceDimension::kAcceleratorNanoseconds, 250);
    e.account = acc;
    e.fences.publication = PublicationId(1);
    e.subcategory.useful = UsefulReason::kAuthoritativeExecution;
    led.append(e);
    std::printf("before restart: digest=%s entries=%zu\n", led.digest().hex().c_str(), led.entry_count());
    led.save(path);
  }
  // Simulate coordinator restart: a fresh incarnation reloads exact history.
  {
    EfficiencyLedger led2;
    Status st = led2.load(path);
    std::printf("after restart: load=%s digest=%s entries=%zu\n", st.ok() ? "ok" : "fail",
                led2.digest().hex().c_str(), led2.entry_count());
    led2.advance_epoch();
    LedgerEntry stale;
    stale.id = AccountingEntryId(2);
    stale.event = EventType::kOutputPublished;
    stale.classification = EfficiencyClass::kUseful;
    stale.usage.set(ResourceDimension::kAcceleratorNanoseconds, 1);
    stale.account = acc;
    stale.fences.publication = PublicationId(2);
    stale.fences.epoch = CoordinatorEpoch(1);  // old epoch
    std::printf("old-epoch append accepted? %s (expect NO)\n", led2.append(stale).ok() ? "YES" : "NO");
    std::printf("reconcile=%s\n", led2.reconcile().closed ? "closed" : "OPEN");
  }
  std::filesystem::remove(path);
  return 0;
}
