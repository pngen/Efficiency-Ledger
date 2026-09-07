#include "efficiency_ledger/model.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "testutil.hpp"

using namespace el;

namespace {

LedgerEntry mk(std::uint64_t id, std::uint64_t ns) {
  LedgerEntry e;
  e.id = AccountingEntryId(id);
  e.event = EventType::kOutputPublished;
  e.classification = EfficiencyClass::kUseful;
  e.usage.set(ResourceDimension::kAcceleratorNanoseconds, ns);
  e.account = AccountId(3);
  e.fences.publication = PublicationId(id + 1);
  e.attempt.id = AttemptId(id);
  e.attempt_outcome = AttemptOutcome::kSucceeded;
  e.subcategory.useful = UsefulReason::kAuthoritativeExecution;
  return e;
}

void concurrency_stress() {
  EfficiencyLedger led;
  const int kThreads = 8;
  const int kPerThread = 5000;
  std::uint64_t expected_ns = 0;
  for (int t = 0; t < kThreads; ++t)
    for (int j = 0; j < kPerThread; ++j)
      expected_ns += static_cast<std::uint64_t>((t * kPerThread + j + 1) % 997);

  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t) {
    ts.emplace_back([&led, t]() {
      for (int j = 0; j < kPerThread; ++j) {
        std::uint64_t id = static_cast<std::uint64_t>(t * kPerThread + j + 1);
        led.append(mk(id, id % 997));
      }
    });
  }
  for (auto& th : ts) th.join();

  CHECK_EQ(led.entry_count(), static_cast<std::size_t>(kThreads * kPerThread));
  auto s = led.account_summary(AccountId(3));
  CHECK_EQ(s.physical[static_cast<std::size_t>(ResourceDimension::kAcceleratorNanoseconds)], expected_ns);
  CHECK_EQ(s.useful[static_cast<std::size_t>(ResourceDimension::kAcceleratorNanoseconds)], expected_ns);
  auto r = led.reconcile();
  CHECK(r.closed);
}

void duplicate_race() {
  EfficiencyLedger led;
  std::atomic<int> ok_count{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < 16; ++t) {
    ts.emplace_back([&led, &ok_count]() {
      if (led.append(mk(777, 100)).ok()) ++ok_count;
    });
  }
  for (auto& th : ts) th.join();
  CHECK_EQ(ok_count.load(), 1);
  CHECK_EQ(led.entry_count(), 1u);
  CHECK_EQ(led.account_summary(AccountId(3)).physical[0], 100);
}

void shutdown_race() {
  EfficiencyLedger led;
  std::atomic<int> accepted{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t) {
    ts.emplace_back([&led, &accepted, t]() {
      if (led.append(mk(1000 + t, 50)).ok()) ++accepted;
    });
  }
  led.set_shutting_down();
  for (auto& th : ts) th.join();
  // Any append that raced the shutdown either committed (counted) or was rejected.
  CHECK(accepted.load() <= 4);
  // New append after shutdown is always rejected, and totals never change twice.
  CHECK(!led.append(mk(9999, 1)).ok());
  auto r = led.reconcile();
  CHECK(r.closed);
}

void persistence_race() {
  EfficiencyLedger led;
  std::atomic<bool> stop{false};
  std::thread writer([&led, &stop]() {
    std::uint64_t i = 0;
    while (!stop.load()) {
      led.append(mk(50000 + (i++), 100));
    }
  });
  std::string snapshots[4];
  for (int i = 0; i < 4; ++i) {
    led.save_to_string(snapshots[i]);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  stop.store(true);
  writer.join();
  CHECK(led.reconcile().closed);
  // Each snapshot must itself load into a closed, consistent ledger.
  for (int i = 0; i < 4; ++i) {
    EfficiencyLedger l2;
    CHECK(l2.load_from_string(snapshots[i], nullptr).ok());
    CHECK(l2.reconcile().closed);
  }
}

}  // namespace

int main() {
  concurrency_stress();
  duplicate_race();
  shutdown_race();
  persistence_race();
  TEST_MAIN_END();
}
