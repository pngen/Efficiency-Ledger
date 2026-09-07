// el_bench.cpp
//
// Throughput benchmark for the Efficiency Ledger core operations.
// Measures COMPLETED work (events/sec) for the core ledger operations at event
// scales 100, 1'000, 10'000, 100'000 and 1'000'000. No timeouts are used: every
// scale either completes or is reported as skipped; nothing is left partially
// counted. Ledgers are fresh RAII objects per benchmark.

#include "efficiency_ledger/model.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

struct Row { const char* name; std::uint64_t events; double ms; };

el::LedgerEntry make_entry(std::uint64_t i) {
  el::LedgerEntry e;
  e.id = el::AccountingEntryId(i + 1);
  if (i % 2 == 0) {
    e.event = el::EventType::kExecutionCompleted;
    e.classification = el::EfficiencyClass::kUseful;
    e.subcategory.useful = el::UsefulReason::kAcceptedOutput;
    e.usage.set(el::ResourceDimension::kAcceleratorNanoseconds, 1000u + i);
    e.fences.publication = el::PublicationId(i + 1);
  } else {
    e.event = el::EventType::kAttemptFailed;
    e.classification = el::EfficiencyClass::kAvoidableWaste;
    e.subcategory.waste = el::WasteReason::kFailedAttempt;
    e.usage.set(el::ResourceDimension::kCpuNanoseconds, 500u + i);
    e.attempt = el::AttemptOptions(el::AttemptId(i + 1), el::AttemptGeneration(1));
    e.attempt_outcome = el::AttemptOutcome::kFailed;
  }
  e.account = el::AccountId(3);
  e.provenance = el::Provenance::kMeasured;
  e.freshness = el::Freshness::kCurrent;
  return e;
}

double print_row(const char* name, std::uint64_t events, double ms) {
  const double eps = (ms > 0.0) ? (static_cast<double>(events) * 1000.0 / ms) : 0.0;
  std::printf("%-28s %12llu %14.3f %16.0f\n", name, static_cast<unsigned long long>(events), ms, eps);
  return eps;
}

bool fill(el::EfficiencyLedger& ledger, std::uint64_t count) {
  for (std::uint64_t i = 0; i < count; ++i) if (!ledger.append(make_entry(i)).ok()) return false;
  return ledger.entry_count() == count;
}

// Populate a ledger for the aggregation benchmark. The canonical entries are
// spread across many distinct accounts so account_summaries()+reconcile()
// perform real per-account work that scales with the account count. The number
// of distinct accounts is bounded (reusing the shared-work participant cap) so
// the benchmark models a realistic multi-account ledger without unbounded
// memory growth.
bool fill_agg(el::EfficiencyLedger& ledger, std::uint64_t count) {
  const std::uint64_t accounts = std::min<std::uint64_t>(count, el::kMaxSharedParticipants);
  for (std::uint64_t i = 0; i < count; ++i) {
    el::LedgerEntry e = make_entry(i);
    e.account = el::AccountId(1 + (i % accounts));
    if (!ledger.append(e).ok()) return false;
  }
  return ledger.entry_count() == count;
}

Row bench_append(std::uint64_t n) {
  el::EfficiencyLedger ledger;
  const auto t0 = Clock::now();
  std::uint64_t appended = 0;
  for (std::uint64_t i = 0; i < n; ++i) if (ledger.append(make_entry(i)).ok()) ++appended;
  const auto t1 = Clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const bool ok = (appended == n && ledger.entry_count() == n);
  std::printf("  append   verify: %s (entry_count=%zu)\n", ok ? "OK" : "MISMATCH", ledger.entry_count());
  return {"event_append", appended, ms};
}

Row bench_dedup(std::uint64_t n) {
  el::EfficiencyLedger ledger;
  if (!fill(ledger, n)) { std::printf("  dedup    verify: MISMATCH\n"); return {"dedup_lookup", 0, 0.0}; }
  const auto t0 = Clock::now();
  std::uint64_t rejected = 0;
  for (std::uint64_t i = 0; i < n; ++i) {
    if (ledger.append(make_entry(i)).code() == el::ErrorCode::kDuplicateEntry) ++rejected;
  }
  const auto t1 = Clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  std::printf("  dedup    verify: %s (rejects=%llu)\n", (rejected == n) ? "OK" : "MISMATCH",
              static_cast<unsigned long long>(rejected));
  return {"dedup_lookup", rejected, ms};
}

Row bench_aggregation(std::uint64_t n) {
  el::EfficiencyLedger ledger;
  if (!fill_agg(ledger, n)) { std::printf("  aggregate verify: MISMATCH\n"); return {"account_aggregation", 0, 0.0}; }
  const std::uint64_t expected = std::min<std::uint64_t>(n, el::kMaxSharedParticipants);
  const auto t0 = Clock::now();
  const std::vector<el::AccountSummary> s = ledger.account_summaries();
  const el::Reconciliation rec = ledger.reconcile();
  const auto t1 = Clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  std::printf("  aggregate verify: %s (accounts=%zu, expected=%llu, closed=%d)\n",
              (rec.closed && s.size() == expected) ? "OK" : "MISMATCH", s.size(),
              static_cast<unsigned long long>(expected), static_cast<int>(rec.closed));
  return {"account_aggregation", static_cast<std::uint64_t>(s.size()), ms};
}

Row bench_shared(std::uint64_t n) {
  const std::uint64_t accounts = std::min<std::uint64_t>(n, el::kMaxSharedParticipants);
  const bool capped = accounts < n;
  el::LedgerEntry e;
  e.id = el::AccountingEntryId(1);
  e.event = el::EventType::kExecutionCompleted;
  e.classification = el::EfficiencyClass::kUseful;
  e.subcategory.useful = el::UsefulReason::kAcceptedOutput;
  e.usage.set(el::ResourceDimension::kAcceleratorNanoseconds, 1000000u);
  e.fences.publication = el::PublicationId(1);
  e.account = el::AccountId(3);
  e.provenance = el::Provenance::kMeasured;
  e.freshness = el::Freshness::kCurrent;
  el::SharedRef shared;
  shared.policy = el::AttributionPolicy::kByWeight;
  shared.participants.reserve(static_cast<std::size_t>(accounts));
  for (std::uint64_t k = 1; k <= accounts; ++k) {
    el::SharedParticipant p;
    p.account = el::AccountId(k);
    p.weight = k;
    shared.participants.push_back(p);
  }
  e.shared = shared;
  el::EfficiencyLedger ledger;
  const auto t0 = Clock::now();
  const el::Status st = ledger.append(e);
  const auto t1 = Clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const el::Reconciliation rec = ledger.reconcile();
  const bool ok = st.ok() && rec.closed;
  std::printf("  shared   verify: %s (accounts=%llu, closed=%d)%s\n", ok ? "OK" : "MISMATCH",
              static_cast<unsigned long long>(accounts), static_cast<int>(rec.closed),
              capped ? "  [capped]" : "");
  return {capped ? "shared_attribution(cap)" : "shared_attribution", accounts, ms};
}

Row bench_replay(std::uint64_t n) {
  el::EfficiencyLedger ledger;
  if (!fill(ledger, n)) { std::printf("  replay   verify: MISMATCH\n"); return {"history_replay", 0, 0.0}; }
  const auto t0 = Clock::now();
  const std::vector<el::LedgerEntry> history = ledger.history();
  el::EfficiencyLedger fresh;
  const el::Status st = fresh.load_history(history);
  const auto t1 = Clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const bool ok = st.ok() && fresh.entry_count() == n;
  std::printf("  replay   verify: %s (history=%zu, replay=%zu)\n", ok ? "OK" : "MISMATCH",
              history.size(), fresh.entry_count());
  return {"history_replay", n, ms};
}

Row bench_persist(std::uint64_t n) {
  el::EfficiencyLedger ledger;
  if (!fill(ledger, n)) { std::printf("  persist  verify: MISMATCH\n"); return {"persistence_save_load", 0, 0.0}; }
  std::string bytes;
  const auto t0 = Clock::now();
  const el::Status save_st = ledger.save_to_string(bytes);
  el::EfficiencyLedger fresh;
  const el::Status load_st = fresh.load_from_string(bytes);
  const auto t1 = Clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const bool ok = save_st.ok() && load_st.ok() && fresh.entry_count() == n;
  std::printf("  persist  verify: %s (bytes=%zu, loaded=%zu)\n", ok ? "OK" : "MISMATCH", bytes.size(),
              fresh.entry_count());
  return {"persistence_save_load", n, ms};
}

Row bench_digest(std::uint64_t n) {
  el::EfficiencyLedger ledger;
  if (!fill(ledger, n)) { std::printf("  digest   verify: MISMATCH\n"); return {"digest", 0, 0.0}; }
  const auto t0 = Clock::now();
  const el::Digest d = ledger.digest();
  const auto t1 = Clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  std::printf("  digest   verify: %s (digest=%s)\n", d ? "OK" : "MISMATCH", d.hex().c_str());
  return {"digest", n, ms};
}

Row bench_concurrent(std::uint64_t n, int threads) {
  el::EfficiencyLedger ledger;
  std::atomic<std::uint64_t> failed{0};
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(threads));
  const std::uint64_t per = n / static_cast<std::uint64_t>(threads);
  const std::uint64_t extra = n % static_cast<std::uint64_t>(threads);
  const auto t0 = Clock::now();
  for (int t = 0; t < threads; ++t) {
    const std::uint64_t begin = static_cast<std::uint64_t>(t) * per + std::min<std::uint64_t>(static_cast<std::uint64_t>(t), extra);
    const std::uint64_t count = per + (static_cast<std::uint64_t>(t) < extra ? 1 : 0);
    workers.emplace_back([&ledger, &failed, begin, count]() {
      for (std::uint64_t k = 0; k < count; ++k) {
        if (!ledger.append(make_entry(begin + k)).ok()) ++failed;
      }
    });
  }
  for (auto& w : workers) w.join();
  const auto t1 = Clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const el::Reconciliation rec = ledger.reconcile();
  const bool ok = (failed.load() == 0) && (ledger.entry_count() == n) && rec.closed;
  std::printf("  concurrent verify: %s (threads=%d, entry_count=%zu, closed=%d, failed=%llu)\n",
              ok ? "OK" : "MISMATCH", threads, ledger.entry_count(), static_cast<int>(rec.closed),
              static_cast<unsigned long long>(failed.load()));
  return {"concurrent_append(8t)", n, ms};
}
}  // namespace

int main() {
  constexpr std::uint64_t kScales[] = {100, 1000, 10000, 100000, 1000000};
  constexpr int kThreads = 8;
  std::printf("Efficiency Ledger throughput benchmark\n");
  std::printf("Operation = completed work; scales are entry counts.\n");
  std::printf("Work unit: event_append/dedup/replay/persist/digest/concurrent report entries;\n");
  std::printf("           account_aggregation and shared_attribution report accounts aggregated.\n");
  std::printf("Model cap on shared-work participants: %zu\n\n", el::kMaxSharedParticipants);
  std::printf("%-28s %12s %14s %16s\n", "benchmark", "events", "ms", "events/sec");
  std::printf("%-28s %12s %14s %16s\n", "----------", "------", "--", "----------");
  for (const std::uint64_t n : kScales) {
    std::printf("\n== scale %llu ==\n", static_cast<unsigned long long>(n));
    const Row ap = bench_append(n);             print_row(ap.name, ap.events, ap.ms);
    const Row dd = bench_dedup(n);              print_row(dd.name, dd.events, dd.ms);
    const Row ag = bench_aggregation(n);        print_row(ag.name, ag.events, ag.ms);
    const Row sh = bench_shared(n);             print_row(sh.name, sh.events, sh.ms);
    const Row rp = bench_replay(n);             print_row(rp.name, rp.events, rp.ms);
    const Row ps = bench_persist(n);            print_row(ps.name, ps.events, ps.ms);
    const Row dg = bench_digest(n);             print_row(dg.name, dg.events, dg.ms);
    const Row cc = bench_concurrent(n, kThreads); print_row(cc.name, cc.events, cc.ms);
  }
  std::printf("\nAll requested scales ran to completion; none were skipped.\n");
  return 0;
}
