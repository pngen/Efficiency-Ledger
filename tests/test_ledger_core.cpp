#include "efficiency_ledger/model.hpp"

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

LedgerEntry avoided(std::uint64_t id, EventType ev, std::uint64_t ns, AccountId acc) {
  LedgerEntry e;
  e.id = AccountingEntryId(id);
  e.event = ev;
  e.classification = EfficiencyClass::kAvoidedWork;
  e.avoided_work = ResourceUsage(ResourceDimension::kAcceleratorNanoseconds, ns);
  e.account = acc;
  e.subcategory.avoided = AvoidedWorkReason::kCacheHit;
  return e;
}

LedgerEntry stranded(std::uint64_t id, EventType ev, std::uint64_t bytes, AccountId acc) {
  LedgerEntry e;
  e.id = AccountingEntryId(id);
  e.event = ev;
  e.classification = EfficiencyClass::kStrandedCapacity;
  e.stranded_capacity = ResourceUsage(ResourceDimension::kAcceleratorMemoryByteNanoseconds, bytes);
  e.account = acc;
  e.subcategory.stranded = StrandedReason::kFragmentation;
  return e;
}

void test_useful_and_retry() {
  EfficiencyLedger led;
  const AccountId acc(42);
  // attempt 1 fails: 300 ms
  CHECK(led.append(mk(1, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 300,
                      acc, 0, AttemptId(11), AttemptOutcome::kFailed)).ok());
  // attempt 2 fails: 250 ms
  CHECK(led.append(mk(2, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 250,
                      acc, 0, AttemptId(12), AttemptOutcome::kFailed)).ok());
  // attempt 3 succeeds and publishes: 200 ms
  CHECK(led.append(mk(3, EventType::kOutputPublished, EfficiencyClass::kUseful, 200,
                      acc, 7, AttemptId(13), AttemptOutcome::kSucceeded)).ok());

  const auto s = led.account_summary(acc);
  CHECK_EQ(s.physical[static_cast<std::size_t>(ResourceDimension::kAcceleratorNanoseconds)], 750);
  CHECK_EQ(s.useful[static_cast<std::size_t>(ResourceDimension::kAcceleratorNanoseconds)], 200);
  CHECK_EQ(s.waste[static_cast<std::size_t>(ResourceDimension::kAcceleratorNanoseconds)], 550);
  auto r = led.reconcile();
  CHECK(r.closed);
  CHECK_EQ(led.entry_count(), 3u);
}

void test_duplicate_rejected() {
  EfficiencyLedger led;
  const AccountId acc(7);
  CHECK(led.append(mk(1, EventType::kOutputPublished, EfficiencyClass::kUseful, 100, acc, 1)).ok());
  const auto s1 = led.account_summary(acc);
  // Duplicate append of the same entry id must not change totals.
  CHECK(!led.append(mk(1, EventType::kOutputPublished, EfficiencyClass::kUseful, 100, acc, 1)).ok());
  const auto s2 = led.account_summary(acc);
  CHECK_EQ(s1.physical[0], s2.physical[0]);
  CHECK_EQ(led.entry_count(), 1u);
}

void test_stale_epoch_and_boot() {
  EfficiencyLedger led;
  const AccountId acc(5);
  led.register_worker(WorkerBootId(100));
  // valid entry
  LedgerEntry ok = mk(1, EventType::kOutputPublished, EfficiencyClass::kUseful, 10, acc, 1);
  ok.fences.epoch = CoordinatorEpoch(1);
  ok.fences.worker_boot = WorkerBootId(100);
  CHECK(led.append(ok).ok());
  // stale epoch
  LedgerEntry stale = mk(2, EventType::kOutputPublished, EfficiencyClass::kUseful, 10, acc, 1);
  stale.fences.epoch = CoordinatorEpoch(99);
  CHECK(!led.append(stale).ok());
  // unknown boot id
  LedgerEntry badboot = mk(3, EventType::kOutputPublished, EfficiencyClass::kUseful, 10, acc, 1);
  badboot.fences.worker_boot = WorkerBootId(999);
  CHECK(!led.append(badboot).ok());
  // epoch advances, old epoch rejected
  led.advance_epoch();
  LedgerEntry old = mk(4, EventType::kOutputPublished, EfficiencyClass::kUseful, 10, acc, 1);
  old.fences.epoch = CoordinatorEpoch(1);
  CHECK(!led.append(old).ok());
}

void test_terminal_attempt_immutable() {
  EfficiencyLedger led;
  const AccountId acc(9);
  CHECK(led.append(mk(1, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 300,
                      acc, 0, AttemptId(21), AttemptOutcome::kFailed)).ok());
  // A later stale completion cannot reclassify the terminal failed attempt as useful.
  CHECK(!led.append(mk(2, EventType::kExecutionCompleted, EfficiencyClass::kUseful, 300,
                       acc, 5, AttemptId(21), AttemptOutcome::kSucceeded)).ok());
  // The failure bucket is unchanged.
  const auto s = led.account_summary(acc);
  CHECK_EQ(s.waste[0], 300);
  CHECK_EQ(s.useful[0], 0);
}

void test_shared_attribution_exact() {
  EfficiencyLedger led;
  const AccountId a(1), b(2), c(3);
  LedgerEntry e;
  e.id = AccountingEntryId(1);
  e.event = EventType::kWorkAccepted;
  e.classification = EfficiencyClass::kUseful;
  e.usage.set(ResourceDimension::kAcceleratorNanoseconds, 100);
  e.fences.publication = PublicationId(1);
  SharedRef sh;
  sh.policy = AttributionPolicy::kEqualShare;
  sh.participants = {{a, 1}, {b, 1}, {c, 1}};
  e.shared = sh;
  CHECK(led.append(e).ok());
  CHECK_EQ(led.account_summary(a).useful[0], 34);
  CHECK_EQ(led.account_summary(b).useful[0], 33);
  CHECK_EQ(led.account_summary(c).useful[0], 33);
  CHECK_EQ(led.account_summary(a).physical[0] + led.account_summary(b).physical[0] + led.account_summary(c).physical[0], 100);
  auto r = led.reconcile();
  CHECK(r.closed);
}

void test_avoided_and_stranded() {
  EfficiencyLedger led;
  const AccountId acc(77);
  CHECK(led.append(avoided(1, EventType::kCacheHit, 500, acc)).ok());
  CHECK(led.append(stranded(2, EventType::kCapacityStranded, 4096, acc)).ok());
  const auto s = led.account_summary(acc);
  CHECK_EQ(s.avoided[static_cast<std::size_t>(ResourceDimension::kAcceleratorNanoseconds)], 500);
  CHECK_EQ(s.stranded[static_cast<std::size_t>(ResourceDimension::kAcceleratorMemoryByteNanoseconds)], 4096);
  CHECK_EQ(s.physical[0], 0);
  const auto r = led.reconcile();
  CHECK(r.closed);
}

void test_reclassification() {
  EfficiencyLedger led;
  const AccountId acc(3);
  // Initially UNKNOWN (ambiguous completion): physical known, classification unknown.
  CHECK(led.append(mk(1, EventType::kExecutionCompleted, EfficiencyClass::kUnknown, 400,
                      acc, 0, AttemptId(31), AttemptOutcome::kSucceeded)).ok());
  auto s = led.account_summary(acc);
  CHECK_EQ(s.unknown[0], 400);
  // Later authoritative publication proves it useful.
  ReclassifyRequest req;
  req.revision_id = AccountingEntryId(2);
  req.target = AccountingEntryId(1);
  req.new_classification = EfficiencyClass::kUseful;
  req.new_subcategory.useful = UsefulReason::kAcceptedOutput;
  req.provenance = Provenance::kMeasured;
  CHECK(led.reclassify(req).ok());
  s = led.account_summary(acc);
  CHECK_EQ(s.unknown[0], 0);
  CHECK_EQ(s.useful[0], 400);
  CHECK_EQ(s.physical[0], 400);
  auto r = led.reconcile();
  CHECK(r.closed);
}

void test_persistence_roundtrip_and_digest() {
  EfficiencyLedger led;
  const AccountId acc(11);
  CHECK(led.append(mk(1, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 300, acc, 0, AttemptId(41), AttemptOutcome::kFailed)).ok());
  CHECK(led.append(mk(2, EventType::kOutputPublished, EfficiencyClass::kUseful, 200, acc, 1, AttemptId(42), AttemptOutcome::kSucceeded)).ok());

  const Digest d1 = led.digest();
  std::string bytes;
  CHECK(led.save_to_string(bytes).ok());

  EfficiencyLedger led2;
  CHECK(!led2.entry_count());
  std::string err;
  CHECK(led2.load_from_string(bytes, &err).ok());
  CHECK_EQ(led2.entry_count(), 2u);
  CHECK(led2.digest() == d1);
  const auto s = led2.account_summary(acc);
  CHECK_EQ(s.physical[0], 500);
  CHECK_EQ(s.useful[0], 200);
  CHECK_EQ(s.waste[0], 300);
  auto r = led2.reconcile();
  CHECK(r.closed);
}

void test_replay_determinism() {
  EfficiencyLedger led;
  const AccountId acc(21);
  CHECK(led.append(mk(1, EventType::kOutputPublished, EfficiencyClass::kUseful, 50, acc, 1)).ok());
  CHECK(led.append(mk(2, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 30, acc, 0, AttemptId(51), AttemptOutcome::kFailed)).ok());
  const Digest d = led.digest();
  const auto hist = led.history();

  EfficiencyLedger led2;
  CHECK(led2.load_history(hist).ok());
  CHECK(led2.digest() == d);
  CHECK(led2.reconcile().closed);
}

void test_digest_sensitive_to_order() {
  EfficiencyLedger a, b;
  const AccountId acc(5);
  CHECK(a.append(mk(1, EventType::kOutputPublished, EfficiencyClass::kUseful, 10, acc, 1)).ok());
  CHECK(a.append(mk(2, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 10, acc, 0, AttemptId(61), AttemptOutcome::kFailed)).ok());
  CHECK(b.append(mk(2, EventType::kAttemptFailed, EfficiencyClass::kAvoidableWaste, 10, acc, 0, AttemptId(61), AttemptOutcome::kFailed)).ok());
  CHECK(b.append(mk(1, EventType::kOutputPublished, EfficiencyClass::kUseful, 10, acc, 1)).ok());
  CHECK(a.digest() != b.digest());
}

void test_corruption_rejection() {
  EfficiencyLedger led;
  const AccountId acc(3);
  CHECK(led.append(mk(1, EventType::kOutputPublished, EfficiencyClass::kUseful, 10, acc, 1)).ok());
  std::string bytes;
  CHECK(led.save_to_string(bytes).ok());

  // Truncated.
  CHECK(!led.load_from_string(bytes.substr(0, bytes.size() / 2), nullptr).ok());
  // Trailing garbage.
  std::string garbled = bytes;
  garbled.push_back('\0');
  CHECK(!led.load_from_string(garbled, nullptr).ok());
  // Corruption in the middle.
  std::string corrupt = bytes;
  const std::size_t idx = corrupt.size() / 2;
  corrupt[idx] = static_cast<char>(corrupt[idx] ^ 0xFF);
  CHECK(!led.load_from_string(corrupt, nullptr).ok());
  // Unknown version.
  std::string bad_ver = bytes;
  bad_ver[4] = static_cast<char>(0xFF);
  CHECK(!led.load_from_string(bad_ver, nullptr).ok());
}

void test_checked_add_and_mul() {
  std::uint64_t out = 0;
  CHECK(checked_add(10, 20, out));
  CHECK_EQ(out, 30);
  CHECK(!checked_add(std::numeric_limits<std::uint64_t>::max(), 1, out));
  CHECK(!checked_mul(std::numeric_limits<std::uint64_t>::max(), 2, out));
  CHECK(checked_mul(1000, 1000, out));
  CHECK_EQ(out, 1000000);
  // mul_div exactness
  std::uint64_t rem = 0;
  CHECK_EQ(mul_div(100, 1, 3, rem), 33);
  CHECK_EQ(rem, 1);
  CHECK_EQ(mul_div(100, 3, 3, rem), 100);
  CHECK_EQ(rem, 0);
}

}  // namespace

int main() {
  test_useful_and_retry();
  test_duplicate_rejected();
  test_stale_epoch_and_boot();
  test_terminal_attempt_immutable();
  test_shared_attribution_exact();
  test_avoided_and_stranded();
  test_reclassification();
  test_persistence_roundtrip_and_digest();
  test_replay_determinism();
  test_digest_sensitive_to_order();
  test_corruption_rejection();
  test_checked_add_and_mul();
  TEST_MAIN_END();
}
