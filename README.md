# Efficiency Ledger

Efficiency Ledger is an open-source, vendor-neutral C++20 runtime for attributing
useful versus wasted accelerator work across execution, retries, recomputation,
residency, transfers, caching, fragmentation, recovery, and stranded capacity.

## Systems question

> Of the accelerator infrastructure consumed by this workload and runtime, what
> actually contributed to authoritative useful output, what was necessary
> overhead, what was avoidable waste, what work was reused or avoided, and where
> did the remaining capacity disappear?

The thesis is that **utilization is not efficiency**. A GPU may report 95%
utilization while most work is recomputation; a worker may stay busy retrying
failed attempts that never contribute to accepted output; a warm model may hold
expensive accelerator memory idle for hours; a transfer may be necessary,
avoidable, duplicated, or completely wasted after stale authority; a cache miss
may force reconstruction a valid reuse hit could have avoided; fragmentation may
strand enough memory to prevent useful work despite substantial aggregate free
capacity; a failed recovery may consume accelerator time, bandwidth, energy,
memory, and storage without producing authoritative progress.

## Exact boundary

Efficiency Ledger owns cross-runtime efficiency accounting, useful-work
attribution, necessary-overhead attribution, avoidable-waste attribution,
avoided-work attribution, stranded-capacity attribution, execution-attempt
efficiency, retry waste, recomputation waste, failed-attempt waste,
cancelled-work waste, stale-authority waste, recovery overhead, transfer
overhead, duplicate-transfer waste, idle-residency overhead, idle
reservation/capacity holding, cache-hit/reuse benefit, cache-miss reconstruction
cost, fragmentation-stranded capacity, unexecutable reserved capacity, useful
versus non-useful accelerator time, transfer bytes, memory-byte-time, energy
(where measured), operation counts, deterministic attribution across shared work,
exact reconciliation, provenance, replay, persistence, restart, stale
evidence/authority rejection, historical efficiency reconstruction, and
structured explanation.

It does **not** own cost optimization, execution-plan selection, generic pricing,
SLO definition, tail-latency governance, resource scheduling, resource
arbitration, capacity prediction, fragmentation remediation, interference
detection, utilization dashboards, generic per-request inference billing, or the
mechanism implementation for retries/recovery/cache/residency/transfers.

## Relationship to adjacent layers

- **Cost Governor** is the previous architectural layer; it decides which legal
  execution plan is preferable under monetary/resource budgets. Efficiency
  Ledger supplies measured waste/useful-work evidence but is not a cost
  optimizer.
- **Utilization Observatory** is the next architectural layer; it explains where
  accelerator capacity is going and why headline utilization differs from useful
  work. Efficiency Ledger provides the exact accounting facts such an
  observatory may consume; it does not absorb it.
- **Inference Ledger** already owns exact per-inference-request accounting.
  Efficiency Ledger is broader and orthogonal: workload-neutral,
  cross-runtime, and focused on useful/overhead/waste/avoided/stranded
  classification. It does not rebuild Inference Ledger under a new name.

## Efficiency classes

Explicit typed classes: USEFUL, NECESSARY_OVERHEAD, AVOIDABLE_WASTE,
AVOIDED_WORK, STRANDED_CAPACITY, UNKNOWN. Subcategories are separate typed enums
(UsefulReason, OverheadReason, WasteReason, AvoidedWorkReason, StrandedReason)
and are never encoded as opaque strings. UNKNOWN remains possible; classification
is never forced without sufficient evidence.

## Resource dimensions

Strongly typed dimensions include AcceleratorNanoseconds, CpuNanoseconds,
EnergyMicroJoules, TransferBytes, StorageBytes, MemoryByteNanoseconds,
AcceleratorMemoryByteNanoseconds, HostMemoryByteNanoseconds,
PinnedMemoryByteNanoseconds, NetworkByteNanoseconds, Operations, Tokens,
Requests, WorkUnits, CapacityByteNanoseconds, and DeviceNanoseconds. Percentages
are never stored; they are derived views over these exact totals.

## Physical versus useful/waste

Physical consumption is kept distinct from classification. A kernel merely
completing does not make its work useful. A GPU ran 500 ms entry remains 500 ms
of physical history even if later classified as AVOIDABLE_WASTE / FAILED_ATTEMPT.
Useful work is authority-bound: a USEFUL classification requires an
authoritative publication id or a valid reuse event. A stale, superseded,
duplicated, rolled-back, or never-published completion is not useful.

## Necessary overhead

Necessary overhead is not waste. Required transfers, required checkpoints,
required recovery coordination, required replication, required warmup, and
required coordination are represented by NECESSARY_OVERHEAD with an
OverheadReason. Not all non-output execution is waste.

## Avoidable waste

Avoidable waste is explicit and typed: FAILED_ATTEMPT, DUPLICATE_ATTEMPT,
STALE_ATTEMPT, REJECTED_COMPLETION, CANCELLED_AFTER_WORK, RECOMPUTATION,
DUPLICATE_TRANSFER, UNUSED_PREFETCH, INVALID_REUSE, EXPIRED_RESIDENCY,
ABANDONED_RECOVERY, ROLLED_BACK_WORK, SPECULATION_REJECTED,
POLICY_MISPREDICTION, IDLE_RESERVED_CAPACITY, FRAGMENTATION_LOSS. Avoidability
is never claimed where evidence cannot establish it; UNKNOWN is preferred to
fabricated certainty.

## Avoided work

Avoided work is a first-class positive counterfactual quantity, never a negative
physical quantity. It records what would otherwise have occurred, why it was
avoided, and its evidence/provenance, and is tracked separately from
physical-consumption reconciliation.

## Stranded capacity

Stranded capacity accounts for capacity that exists physically but cannot
produce useful work (FRAGMENTATION, RESERVATION_IDLE, UNUSABLE_HEADROOM,
TOPOLOGY_MISMATCH, INCOMPATIBLE_DEVICE, PINNED_BUT_UNUSED, RESIDENCY_HOLD,
CAPACITY_FENCED, MEMORY_HEADROOM, WARM_STANDBY_IDLE, CAPACITY_WITHOUT_DEMAND,
CAPACITY_BLOCKED_BY_CONSTRAINT). Stranded capacity never masquerades as consumed
work and does not perform remediation.

## Shared attribution

Shared work (batch kernels, shared residency, shared KV/prefix state, shared
transfers, shared checkpoints, shared warm engines, shared collectives) is
attributed deterministically with exact residual handling. Policies are
versioned, generation-bound, deterministic, and replayable: DIRECT_OWNERSHIP,
EQUAL_SHARE, BY_BYTES, BY_TOKENS, BY_EXECUTION_TIME, BY_RESERVED_CAPACITY,
BY_WEIGHT, UNATTRIBUTED_SHARED. Each dimension is split with largest-remainder
rounding so attributed amount plus explicit residual equals the physical total
exactly.

## Reconciliation

For each supported resource dimension, physical consumption equals USEFUL +
NECESSARY_OVERHEAD + AVOIDABLE_WASTE + UNKNOWN. Avoided work and stranded
capacity are separate dimensions and never enter physical-consumption
reconciliation. The ledger verifies closure per account and across the ledger,
with no unexplained loss and no double-counting.

## Authority and generations

Mutations are fenced against CoordinatorEpoch, WorkerBootId, LedgerGeneration,
AccountGeneration, WorkloadGeneration, AttemptGeneration, ResourceGeneration,
ReservationGeneration, AllocationGeneration, TransferGeneration,
RecoveryGeneration, CacheGeneration, EvidenceGeneration, AccountingGeneration,
and PublicationId. Stale mutation rejects. Terminal attempts cannot be
resurrected by a stale completion. Duplicate and stale entries never change
totals twice.

## Persistence

Durable, versioned, integrity-checked persistence uses deterministic encoding,
bounded decode, checked arithmetic, CRC-32C integrity, truncation rejection,
corruption rejection, trailing-garbage rejection, unknown-version rejection, and
atomic save/replace. It does not serialize raw structs.

## Replay and digest

Replaying the same canonical event stream reproduces identical account totals,
classifications, reconciliation, and a stable digest. The digest depends only on
the canonical ordered history (not on memory addresses, unordered iteration,
wall-clock noise, or current epoch), so historical digests remain stable across
coordinator restart. Ordering is preserved deterministically.

## Restart

A real coordinator restart loads durable history, advances CoordinatorEpoch,
preserves committed historical accounting, and requires fresh current evidence
before current efficiency state is asserted. Old-epoch mutation traffic rejects.
Surviving workers reconnect with fresh WorkerBootId and republish current
evidence; stale old-boot accounting rejects.

## Distributed proof

Real OS processes communicate over framed TCP loopback. The transport is
versioned, bounded, checksummed, and partial-read/partial-write safe, with
malformed-frame and oversized-frame rejection. A real coordinator process owns
the ledger; real worker processes register their boot id, append entries, and
are actually terminated to prove worker-death fencing. The proof exercises
useful+waste, real worker death with fresh boot id and stale-boot rejection,
duplicate/stale rejection, reuse/avoided work, stranded capacity, and a real
coordinator process kill followed by a fresh incarnation that reloads exact
historical totals with stable digest and epoch advance.

## CUDA proof

Where a real CUDA device is present (NVIDIA RTX 5090, CUDA 13.x, sm_120), the
proof performs real cudaMalloc, H2D, kernel execution, synchronization, D2H, and
CPU parity, and classifies measured device work as USEFUL, FAILED_ATTEMPT waste,
RECOMPUTATION, REUSE/AVOIDED_WORK, and TRANSFER overhead. Device timing honors
the distinction between host submission, queue delay, device execution, transfer,
and host-observed completion; only synchronized completed work counts as physical
consumption. Device memory is verified to return to baseline.

## REAL / DERIVED / SYNTHETIC / UNSUPPORTED

- REAL: real CUDA work, real process kill, real TCP, measured bytes/durations.
- DERIVED: ratios, byte-time, reconciliation, derived avoided-work calculations.
- SYNTHETIC: multi-node capacity, unavailable topology, energy when not
  measurable, cross-device scenarios without hardware.
- UNSUPPORTED: hardware telemetry not available, multi-GPU efficiency claims,
  NVLink/RDMA/MIG efficiency measurements.

Never blurred across categories.

## Benchmarks

The benchmark measures completed work for event append, dedup lookup, account
aggregation, shared attribution, history replay, persistence save/load, digest,
and multithreaded concurrent append at scales up to 1,000,000 events, reporting
events/second. It uses no timeouts and only reports what it completed.

## Install

The project builds a static library and installs headers, the library, a CMake
package config and version file, and the el CLI. Downstream consumers use:

    find_package(EfficiencyLedger CONFIG REQUIRED)
    target_link_libraries(app PRIVATE EfficiencyLedger::EfficiencyLedger)

The installed package exposes the target EfficiencyLedger::EfficiencyLedger
(alias for the exported EfficiencyLedger::efficiency_ledger).

## Examples

Runnable examples cover useful execution, failed-attempt waste, retry accounting,
recomputation, valid reuse / avoided work, shared attribution, stranded/fragmented
capacity, transfer overhead, and restart/replay.

## CLI

The el CLI loads a persisted ledger and provides summary, show-account,
show-waste, show-useful, show-overhead, show-avoided, show-stranded, reconcile,
validate-state, replay, and digest commands. The distributed coordinator and
worker roles are provided by el_coordinator and el_worker.

## Limitations

- A single opaque efficiency score is not provided by default; efficiency is
  vector-valued across dimensions.
- Energy is only accounted when reliably measured; otherwise it is labeled
  UNSUPPORTED or SYNTHETIC, never fabricated.
- Multi-GPU, NVLink, RDMA, and MIG efficiency claims are not made.
- Fragmentation and remediation are recorded but not remediated.
- The ledger does not schedule, arbitrate, cost-optimize, or define SLOs.
- The digest covers the canonical event history; the current coordinator epoch
  is mutable system state that is fenced at mutation time rather than folded into
  the historical digest.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
