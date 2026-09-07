// el_cuda_proof.cu
//
// A REAL hardware efficiency proof on an NVIDIA RTX 5090 (sm_120 / CUDA 12.9+).
//
// It exercises real device memory allocation, H2D transfer, a real scale kernel
// with device timing, D2H transfer, CPU parity verification, and then classifies
// each measured action with el::EfficiencyLedger. Finally it verifies that free
// device memory returns to its baseline and that the ledger reconciles closed.
//
// Builds with nvcc (host cl.exe) + efficiency_ledger + CUDA::cudart:
//   nvcc -std=c++20 -O2 -arch=sm_120 -I<src>/include el_cuda_proof.cu \
//        -o el_cuda_proof.exe <src>/build_rel/efficiency_ledger.lib cudart.lib
//
// Self-contained; only depends on CUDA runtime and the ledger public headers.

#include <cstdint>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>

#include <cuda_runtime.h>

#include "efficiency_ledger/model.hpp"

using namespace el;

namespace {

// ---- Tunables ---------------------------------------------------------------
constexpr std::size_t kElements = 1ull << 26;  // 64 Mi scalar elements
constexpr int kBlock = 256;                    // threads per block
constexpr std::uint64_t kOneMiB = 1ull << 20;
constexpr float kAlpha = 1.00023f;

// ---- Measurement helpers ----------------------------------------------------
struct TimedRun {
  bool ok = false;
  std::uint64_t ns = 0;  // measured nanoseconds
  const char* error = nullptr;
};

// A real, compute-correct kernel: out[i] = alpha * in[i].
// Grid-stride so the launch is robust for any grid geometry; with the chosen
// grid each thread processes exactly one element.
__global__ void saxpy_kernel(float* __restrict__ out, const float* __restrict__ in,
                             float alpha, int n) {
  const int stride = blockDim.x * gridDim.x;
  for (int i = blockIdx.x * blockDim.x + threadIdx.x; i < n; i += stride) {
    out[i] = alpha * in[i];
  }
}

// Time the real device execution of the kernel on a stream with cudaEvents.
// The start and stop events straddle the kernel launch on the same stream, so
// the elapsed time is the GPU device-execution time -- NOT host enqueue latency
// (the work is already queued; the GPU processes start -> kernel -> stop).
TimedRun time_kernel(float* d_out, const float* d_in, float alpha, int n,
                     cudaStream_t stream) {
  TimedRun tr;
  cudaEvent_t start = nullptr, stop = nullptr;
  if (cudaEventCreate(&start) != cudaSuccess) {
    tr.error = cudaGetErrorString(cudaGetLastError());
    return tr;
  }
  if (cudaEventCreate(&stop) != cudaSuccess) {
    tr.error = cudaGetErrorString(cudaGetLastError());
    cudaEventDestroy(start);
    return tr;
  }

  const int grid = static_cast<int>((n + kBlock - 1) / kBlock);
  cudaEventRecord(start, stream);
  saxpy_kernel<<<grid, kBlock, 0, stream>>>(d_out, d_in, alpha, n);
  if (cudaGetLastError() != cudaSuccess) {
    tr.error = cudaGetErrorString(cudaGetLastError());
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    return tr;
  }
  cudaEventRecord(stop, stream);
  if (cudaStreamSynchronize(stream) != cudaSuccess) {
    tr.error = cudaGetErrorString(cudaGetLastError());
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    return tr;
  }

  float ms = 0.0f;
  if (cudaEventElapsedTime(&ms, start, stop) != cudaSuccess) {
    tr.error = cudaGetErrorString(cudaGetLastError());
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    return tr;
  }
  tr.ns = static_cast<std::uint64_t>(static_cast<double>(ms) * 1e6);
  tr.ok = true;

  cudaEventDestroy(start);
  cudaEventDestroy(stop);
  return tr;
}

// Measure the wall-clock cost of a synchronous H2D + D2H transfer pair (the
// bytes that must move to use the device). Timed with cudaEvents around the
// synchronous copies.
TimedRun time_transfer(float* d_in, float* d_out, const float* h_in, float* h_out,
                       std::size_t bytes) {
  TimedRun tr;
  cudaEvent_t start = nullptr, stop = nullptr;
  if (cudaEventCreate(&start) != cudaSuccess) {
    tr.error = cudaGetErrorString(cudaGetLastError());
    return tr;
  }
  if (cudaEventCreate(&stop) != cudaSuccess) {
    tr.error = cudaGetErrorString(cudaGetLastError());
    cudaEventDestroy(start);
    return tr;
  }

  cudaEventRecord(start);
  cudaMemcpy(d_in, h_in, bytes, cudaMemcpyHostToDevice);
  cudaMemcpy(h_out, d_out, bytes, cudaMemcpyDeviceToHost);
  cudaEventRecord(stop);
  if (cudaEventSynchronize(stop) != cudaSuccess) {
    tr.error = cudaGetErrorString(cudaGetLastError());
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    return tr;
  }

  float ms = 0.0f;
  if (cudaEventElapsedTime(&ms, start, stop) != cudaSuccess) {
    tr.error = cudaGetErrorString(cudaGetLastError());
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    return tr;
  }
  tr.ns = static_cast<std::uint64_t>(static_cast<double>(ms) * 1e6);
  tr.ok = true;

  cudaEventDestroy(start);
  cudaEventDestroy(stop);
  return tr;
}

// Append an entry to the ledger; report and signal failure on reject.
int append_or_fail(EfficiencyLedger& led, const LedgerEntry& e) {
  const Status st = led.append(e);
  if (!st.ok()) {
    std::cerr << "LEDGER APPEND FAILED (code " << static_cast<int>(st.code())
              << "): " << st.message() << "\n";
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  int device = 0;
  if (cudaSetDevice(device) != cudaSuccess) {
    std::cerr << "cudaSetDevice failed: " << cudaGetErrorString(cudaGetLastError()) << "\n";
    return 1;
  }

  cudaDeviceProp prop{};
  if (cudaGetDeviceProperties(&prop, device) != cudaSuccess) {
    std::cerr << "cudaGetDeviceProperties failed: "
              << cudaGetErrorString(cudaGetLastError()) << "\n";
    return 1;
  }

  std::cout << "device: " << prop.name << "\n";
  std::cout << "  compute capability: " << prop.major << "." << prop.minor << "\n";
  std::cout << "  total global mem : " << (prop.totalGlobalMem >> 20) << " MiB\n";

  int runtime_version = 0;
  if (cudaRuntimeGetVersion(&runtime_version) == cudaSuccess) {
    std::cout << "  CUDA runtime      : " << (runtime_version / 1000) << "."
              << ((runtime_version % 1000) / 10) << "\n";
  }

  // ---- Host buffers ---------------------------------------------------------
  const std::size_t n = static_cast<std::size_t>(kElements);
  const std::size_t bytes = n * sizeof(float);

  float* h_in = new (std::nothrow) float[n];
  float* h_out = new (std::nothrow) float[n];
  if (!h_in || !h_out) {
    std::cerr << "host allocation failed\n";
    delete[] h_in;
    delete[] h_out;
    return 1;
  }
  for (std::size_t i = 0; i < n; ++i) {
    h_in[i] = static_cast<float>(static_cast<int>(i % 1024) - 512) * 0.25f;
  }

  // ---- Context warm-up (also establishes a stable free-memory baseline) -----
  // Allocate + free a small block so the CUDA context is created and its driver
  // allocations settle before we record the baseline.
  void* warm = nullptr;
  if (cudaMalloc(&warm, 1u << 20) != cudaSuccess || cudaFree(warm) != cudaSuccess) {
    std::cerr << "context warm-up failed: " << cudaGetErrorString(cudaGetLastError()) << "\n";
    delete[] h_in;
    delete[] h_out;
    return 1;
  }

  std::size_t free_baseline = 0, total_base = 0;
  if (cudaMemGetInfo(&free_baseline, &total_base) != cudaSuccess) {
    std::cerr << "cudaMemGetInfo(baseline) failed: "
              << cudaGetErrorString(cudaGetLastError()) << "\n";
    delete[] h_in;
    delete[] h_out;
    return 1;
  }

  EfficiencyLedger led;

  // ---- Device buffers -------------------------------------------------------
  float* d_in = nullptr;
  float* d_out = nullptr;
  if (cudaMalloc(&d_in, bytes) != cudaSuccess || cudaMalloc(&d_out, bytes) != cudaSuccess) {
    std::cerr << "cudaMalloc failed: " << cudaGetErrorString(cudaGetLastError()) << "\n";
    if (d_in) cudaFree(d_in);
    if (d_out) cudaFree(d_out);
    delete[] h_in;
    delete[] h_out;
    return 1;
  }

  cudaStream_t stream = nullptr;
  if (cudaStreamCreate(&stream) != cudaSuccess) {
    std::cerr << "cudaStreamCreate failed: " << cudaGetErrorString(cudaGetLastError()) << "\n";
    cudaFree(d_in);
    cudaFree(d_out);
    delete[] h_in;
    delete[] h_out;
    return 1;
  }

  // Kernel warm-up (untimed) so the measured runs are steady-state.
  {
    const int grid = static_cast<int>((n + kBlock - 1) / kBlock);
    saxpy_kernel<<<grid, kBlock, 0, stream>>>(d_out, d_in, kAlpha, static_cast<int>(n));
    if (cudaStreamSynchronize(stream) != cudaSuccess) {
      std::cerr << "kernel warm-up failed: " << cudaGetErrorString(cudaGetLastError()) << "\n";
      cudaStreamDestroy(stream);
      cudaFree(d_in);
      cudaFree(d_out);
      delete[] h_in;
      delete[] h_out;
      return 1;
    }
  }

  const AccountId account(3);
  std::uint64_t entry_counter = 0;
  std::uint64_t publication_counter = 100;
  std::uint64_t attempt_counter = 1;

  // ===========================================================================
  // 1) USEFUL: allocate, H2D, run real kernel (timed), sync, D2H, verify parity.
  // ===========================================================================
  if (cudaMemcpy(d_in, h_in, bytes, cudaMemcpyHostToDevice) != cudaSuccess) {
    std::cerr << "H2D (useful) failed: " << cudaGetErrorString(cudaGetLastError()) << "\n";
    return 1;
  }

  const TimedRun t1 = time_kernel(d_out, d_in, kAlpha, static_cast<int>(n), stream);
  if (!t1.ok) {
    std::cerr << "useful kernel timing failed: " << t1.error << "\n";
    return 1;
  }

  if (cudaMemcpy(h_out, d_out, bytes, cudaMemcpyDeviceToHost) != cudaSuccess) {
    std::cerr << "D2H (useful) failed: " << cudaGetErrorString(cudaGetLastError()) << "\n";
    return 1;
  }

  // CPU parity verification (the point of the real proof).
  std::size_t mismatches = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const float ref = h_in[i] * kAlpha;
    const float diff = std::fabsf(h_out[i] - ref);
    const float tol = 1e-3f * (1.0f + std::fabsf(ref));
    if (diff > tol) ++mismatches;
  }
  std::cout << "[1] USEFUL kernel: device-exec = " << t1.ns << " ns, mismatches = "
            << mismatches << "\n";
  if (mismatches != 0) {
    std::cerr << "CPU parity verification FAILED (" << mismatches << " mismatches)\n";
    return 1;
  }

  {
    LedgerEntry e;
    e.id = AccountingEntryId(++entry_counter);
    e.event = EventType::kOutputPublished;
    e.classification = EfficiencyClass::kUseful;
    e.subcategory.useful = UsefulReason::kAuthoritativeExecution;
    e.usage.set(ResourceDimension::kAcceleratorNanoseconds, t1.ns);
    e.provenance = Provenance::kMeasured;
    e.account = account;
    e.fences.publication = PublicationId(publication_counter++);
    e.attempt.id = AttemptId(attempt_counter++);
    e.attempt_outcome = AttemptOutcome::kSucceeded;
    if (append_or_fail(led, e)) return 1;
  }

  // ===========================================================================
  // 2) FAILED WASTE: run kernel, discard the result, measure the thrown-away work.
  // ===========================================================================
  const TimedRun t2 = time_kernel(d_out, d_in, kAlpha, static_cast<int>(n), stream);
  if (!t2.ok) {
    std::cerr << "failed-waste kernel timing failed: " << t2.error << "\n";
    return 1;
  }
  std::cout << "[2] FAILED WASTE: discarded kernel = " << t2.ns << " ns\n";
  {
    LedgerEntry e;
    e.id = AccountingEntryId(++entry_counter);
    e.event = EventType::kAttemptFailed;
    e.classification = EfficiencyClass::kAvoidableWaste;
    e.subcategory.waste = WasteReason::kFailedAttempt;
    e.usage.set(ResourceDimension::kAcceleratorNanoseconds, t2.ns);
    e.provenance = Provenance::kMeasured;
    e.account = account;
    e.attempt.id = AttemptId(attempt_counter++);
    e.attempt_outcome = AttemptOutcome::kFailed;
    if (append_or_fail(led, e)) return 1;
  }

  // ===========================================================================
  // 3) RECOMPUTATION: run kernel, discard, re-run the same kernel.
  //    First run = avoidable waste (recomputation), second = useful.
  // ===========================================================================
  const TimedRun t3a = time_kernel(d_out, d_in, kAlpha, static_cast<int>(n), stream);
  if (!t3a.ok) {
    std::cerr << "recompute #1 kernel timing failed: " << t3a.error << "\n";
    return 1;
  }
  const TimedRun t3b = time_kernel(d_out, d_in, kAlpha, static_cast<int>(n), stream);
  if (!t3b.ok) {
    std::cerr << "recompute #2 kernel timing failed: " << t3b.error << "\n";
    return 1;
  }
  std::cout << "[3] RECOMPUTATION: discarded run = " << t3a.ns
            << " ns, kept run = " << t3b.ns << " ns\n";
  {
    LedgerEntry e;
    e.id = AccountingEntryId(++entry_counter);
    e.event = EventType::kExecutionCompleted;
    e.classification = EfficiencyClass::kAvoidableWaste;
    e.subcategory.waste = WasteReason::kRecomputation;
    e.usage.set(ResourceDimension::kAcceleratorNanoseconds, t3a.ns);
    e.provenance = Provenance::kMeasured;
    e.account = account;
    if (append_or_fail(led, e)) return 1;
  }
  {
    LedgerEntry e;
    e.id = AccountingEntryId(++entry_counter);
    e.event = EventType::kOutputPublished;
    e.classification = EfficiencyClass::kUseful;
    e.subcategory.useful = UsefulReason::kAuthoritativeExecution;
    e.usage.set(ResourceDimension::kAcceleratorNanoseconds, t3b.ns);
    e.provenance = Provenance::kMeasured;
    e.account = account;
    e.fences.publication = PublicationId(publication_counter++);
    e.attempt.id = AttemptId(attempt_counter++);
    e.attempt_outcome = AttemptOutcome::kSucceeded;
    if (append_or_fail(led, e)) return 1;
  }

  // ===========================================================================
  // 4) AVOIDED WORK: run the kernel once (measured), then reuse the computed
  //    state without re-running an equivalent kernel. Zero physical usage.
  // ===========================================================================
  const TimedRun t4 = time_kernel(d_out, d_in, kAlpha, static_cast<int>(n), stream);
  if (!t4.ok) {
    std::cerr << "avoided-work kernel timing failed: " << t4.error << "\n";
    return 1;
  }
  std::cout << "[4] AVOIDED WORK: one kernel = " << t4.ns
            << " ns, state reused (no re-run)\n";
  {
    LedgerEntry e;
    e.id = AccountingEntryId(++entry_counter);
    e.event = EventType::kStateReused;
    e.classification = EfficiencyClass::kAvoidedWork;
    e.subcategory.avoided = AvoidedWorkReason::kKernelReuse;
    e.avoided_work = ResourceUsage(ResourceDimension::kAcceleratorNanoseconds, t4.ns);
    // usage remains zero (default) for kAvoidedWork -- required by the ledger.
    e.provenance = Provenance::kMeasured;
    e.account = account;
    if (append_or_fail(led, e)) return 1;
  }

  // ===========================================================================
  // 5) TRANSFER: measure H2D + D2H bytes ns (necessary overhead), then a
  //    duplicated / discarded transfer (avoidable waste).
  // ===========================================================================
  const TimedRun t5 = time_transfer(d_in, d_out, h_in, h_out, bytes);
  if (!t5.ok) {
    std::cerr << "transfer timing failed: " << t5.error << "\n";
    return 1;
  }
  const std::uint64_t transfer_bytes = static_cast<std::uint64_t>(bytes) * 2ull;
  std::cout << "[5a] TRANSFER overhead: " << transfer_bytes << " bytes, "
            << t5.ns << " ns\n";
  {
    LedgerEntry e;
    e.id = AccountingEntryId(++entry_counter);
    e.event = EventType::kTransferCompleted;
    e.classification = EfficiencyClass::kNecessaryOverhead;
    e.subcategory.overhead = OverheadReason::kRequiredTransfer;
    e.usage.set(ResourceDimension::kTransferBytes, transfer_bytes);
    e.usage.set(ResourceDimension::kDeviceNanoseconds, t5.ns);
    e.provenance = Provenance::kMeasured;
    e.account = account;
    e.owners.transfer = TransferId(1);
    if (append_or_fail(led, e)) return 1;
  }

  const TimedRun t5b = time_transfer(d_in, d_out, h_in, h_out, bytes);
  if (!t5b.ok) {
    std::cerr << "duplicate transfer timing failed: " << t5b.error << "\n";
    return 1;
  }
  std::cout << "[5b] DUPLICATE TRANSFER: " << transfer_bytes << " bytes, "
            << t5b.ns << " ns (discarded)\n";
  {
    LedgerEntry e;
    e.id = AccountingEntryId(++entry_counter);
    e.event = EventType::kTransferDiscarded;
    e.classification = EfficiencyClass::kAvoidableWaste;
    e.subcategory.waste = WasteReason::kDuplicateTransfer;
    e.usage.set(ResourceDimension::kTransferBytes, transfer_bytes);
    e.usage.set(ResourceDimension::kDeviceNanoseconds, t5b.ns);
    e.provenance = Provenance::kMeasured;
    e.account = account;
    e.owners.transfer = TransferId(2);
    if (append_or_fail(led, e)) return 1;
  }

  // ---- Device memory baseline check ----------------------------------------
  if (cudaFree(d_in) != cudaSuccess || cudaFree(d_out) != cudaSuccess) {
    std::cerr << "cudaFree failed: " << cudaGetErrorString(cudaGetLastError()) << "\n";
    cudaStreamDestroy(stream);
    delete[] h_in;
    delete[] h_out;
    return 1;
  }
  d_in = nullptr;
  d_out = nullptr;

  std::size_t free_after = 0, total_after = 0;
  if (cudaMemGetInfo(&free_after, &total_after) != cudaSuccess) {
    std::cerr << "cudaMemGetInfo(after) failed: "
              << cudaGetErrorString(cudaGetLastError()) << "\n";
    cudaStreamDestroy(stream);
    delete[] h_in;
    delete[] h_out;
    return 1;
  }

  const std::uint64_t free_delta =
      free_after > free_baseline ? free_after - free_baseline : free_baseline - free_after;
  std::cout << "[6] DEVICE MEMORY: baseline free = " << free_baseline
            << " B, after free = " << free_after << " B, |delta| = " << free_delta
            << " B (tol " << kOneMiB << " B)\n";
  if (free_delta > kOneMiB) {
    std::cerr << "DEVICE MEMORY BASELINE FAILED: free memory did not return to baseline\n";
    cudaStreamDestroy(stream);
    delete[] h_in;
    delete[] h_out;
    return 1;
  }

  if (cudaStreamDestroy(stream) != cudaSuccess) {
    std::cerr << "cudaStreamDestroy failed: " << cudaGetErrorString(cudaGetLastError()) << "\n";
    delete[] h_in;
    delete[] h_out;
    return 1;
  }

  // ---- Reconciliation -------------------------------------------------------
  std::cout << "\n===== Efficiency Ledger reconciliation =====\n";
  const Reconciliation recon = led.reconcile();
  std::cout << "closed: " << (recon.closed ? "yes" : "no") << "\n";
  std::cout << "entries: " << recon.entry_count << "\n";
  if (!recon.messages.empty()) {
    for (const auto& msg : recon.messages) std::cout << "  " << msg << "\n";
  }

  const AccountSummary sum = led.account_summary(account);
  const auto acc = static_cast<std::size_t>(ResourceDimension::kAcceleratorNanoseconds);
  const auto trb = static_cast<std::size_t>(ResourceDimension::kTransferBytes);
  const auto dns = static_cast<std::size_t>(ResourceDimension::kDeviceNanoseconds);
  std::cout << "\n----- per-class totals (account 3) -----\n";
  std::cout << "USEFUL            : acc-ns " << sum.useful[acc] << " | xfer-bytes "
            << sum.useful[trb] << " | dev-ns " << sum.useful[dns] << "\n";
  std::cout << "NECESSARY_OVERHEAD: acc-ns " << sum.overhead[acc] << " | xfer-bytes "
            << sum.overhead[trb] << " | dev-ns " << sum.overhead[dns] << "\n";
  std::cout << "AVOIDABLE_WASTE   : acc-ns " << sum.waste[acc] << " | xfer-bytes "
            << sum.waste[trb] << " | dev-ns " << sum.waste[dns] << "\n";
  std::cout << "AVOIDED_WORK      : acc-ns " << sum.avoided[acc] << " (counterfactual)\n";
  std::cout << "UNKNOWN           : acc-ns " << sum.unknown[acc] << "\n";

  std::cout << "digest: " << led.digest().hex() << "\n";
  std::cout << "energy: UNSUPPORTED\n";

  if (!recon.closed) {
    std::cerr << "RECONCILIATION DID NOT CLOSE\n";
    delete[] h_in;
    delete[] h_out;
    return 1;
  }

  std::cout << "\nCUDA PROOF PASS\n";

  delete[] h_in;
  delete[] h_out;
  return 0;
}
