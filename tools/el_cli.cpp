#include <cstdio>
#include <cstdlib>
#include <string>

#include "efficiency_ledger/ledger.hpp"

using namespace el;

namespace {

const char* dim_name(std::size_t d) {
  return to_string(static_cast<ResourceDimension>(d));
}

void print_summary(const std::vector<AccountSummary>& sums) {
  std::printf("account   physical   useful   overhead    waste   unknown   avoided  stranded\n");
  for (const auto& s : sums) {
    const std::size_t d = static_cast<std::size_t>(ResourceDimension::kAcceleratorNanoseconds);
    std::printf("%-9llu %9llu %8llu %9llu %8llu %8llu %8llu %8llu\n",
                static_cast<unsigned long long>(s.account.value()),
                static_cast<unsigned long long>(s.physical[d]),
                static_cast<unsigned long long>(s.useful[d]),
                static_cast<unsigned long long>(s.overhead[d]),
                static_cast<unsigned long long>(s.waste[d]),
                static_cast<unsigned long long>(s.unknown[d]),
                static_cast<unsigned long long>(s.avoided[d]),
                static_cast<unsigned long long>(s.stranded[d]));
  }
}

void print_class(const std::vector<AccountSummary>& sums, EfficiencyClass cls) {
  const std::size_t d = static_cast<std::size_t>(ResourceDimension::kAcceleratorNanoseconds);
  for (const auto& s : sums) {
    std::uint64_t val = 0;
    switch (cls) {
      case EfficiencyClass::kUseful: val = s.useful[d]; break;
      case EfficiencyClass::kNecessaryOverhead: val = s.overhead[d]; break;
      case EfficiencyClass::kAvoidableWaste: val = s.waste[d]; break;
      case EfficiencyClass::kAvoidedWork: val = s.avoided[d]; break;
      case EfficiencyClass::kStrandedCapacity: val = s.stranded[d]; break;
      default: val = s.unknown[d]; break;
    }
    if (val != 0) std::printf("account %llu: %s = %llu\n",
                              static_cast<unsigned long long>(s.account.value()),
                              to_string(cls), static_cast<unsigned long long>(val));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: el <command> <ledger-file> [account]\n"
                 "commands: summary, show-account, show-waste, show-useful, show-overhead, "
                 "show-avoided, show-stranded, reconcile, validate-state, replay, digest\n");
    return 2;
  }
  const std::string cmd = argv[1];
  const std::string path = argv[2];

  EfficiencyLedger led;
  Status st = led.load(path);
  if (!st.ok()) {
    std::fprintf(stderr, "load failed: %s\n", st.message().c_str());
    return 1;
  }

  const auto sums = led.account_summaries();
  if (cmd == "summary") {
    print_summary(sums);
  } else if (cmd == "digest") {
    std::printf("digest: %s\nentry_count: %zu\n", led.digest().hex().c_str(), led.entry_count());
  } else if (cmd == "reconcile") {
    auto r = led.reconcile();
    std::printf("closed: %s\n", r.closed ? "yes" : "no");
    for (const auto& m : r.messages) std::printf("  %s\n", m.c_str());
  } else if (cmd == "validate-state") {
    auto r = led.reconcile();
    std::printf("validate-state: %s\n", r.closed ? "OK" : "FAILED");
  } else if (cmd == "replay") {
    auto hist = led.history();
    EfficiencyLedger led2;
    st = led2.load_history(hist);
    std::printf("replay: %s\ndigest-match: %s\n", st.ok() ? "OK" : st.message().c_str(),
                (led2.digest() == led.digest()) ? "yes" : "no");
  } else if (cmd == "show-account") {
    if (argc < 4) { std::fprintf(stderr, "show-account requires an account id\n"); return 2; }
    auto s = led.account_summary(AccountId(std::stoull(argv[3])));
    for (std::size_t d = 0; d < kDimensionCount; ++d) {
      if (s.physical[d] || s.useful[d] || s.overhead[d] || s.waste[d] || s.unknown[d] ||
          s.avoided[d] || s.stranded[d]) {
        std::printf("%-32s physical=%llu useful=%llu overhead=%llu waste=%llu unknown=%llu "
                    "avoided=%llu stranded=%llu\n",
                    dim_name(d), static_cast<unsigned long long>(s.physical[d]),
                    static_cast<unsigned long long>(s.useful[d]),
                    static_cast<unsigned long long>(s.overhead[d]),
                    static_cast<unsigned long long>(s.waste[d]),
                    static_cast<unsigned long long>(s.unknown[d]),
                    static_cast<unsigned long long>(s.avoided[d]),
                    static_cast<unsigned long long>(s.stranded[d]));
      }
    }
  } else if (cmd == "show-waste") print_class(sums, EfficiencyClass::kAvoidableWaste);
  else if (cmd == "show-useful") print_class(sums, EfficiencyClass::kUseful);
  else if (cmd == "show-overhead") print_class(sums, EfficiencyClass::kNecessaryOverhead);
  else if (cmd == "show-avoided") print_class(sums, EfficiencyClass::kAvoidedWork);
  else if (cmd == "show-stranded") print_class(sums, EfficiencyClass::kStrandedCapacity);
  else {
    std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
    return 2;
  }
  return 0;
}
