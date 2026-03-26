# Cache Simulator Specification

## 1. Purpose

This document defines the target scope for the cache simulator in this repository.
It serves as the implementation contract for the project and the reference for
future refactors, feature additions, and validation.

The simulator should evolve from the current single-level `L1Cache` prototype
into a configurable multi-level cache simulator with clear policy control,
correctness guarantees, and measurable statistics.

## 2. Project Goals

The project should support:
- Functional cache simulation for reads and writes
- Configurable cache geometry and policy choices
- Multi-level hierarchy behavior
- Clear, testable semantics for hits, misses, fills, evictions, and writebacks
- Statistics suitable for comparing cache configurations

The simulator is intended to be:
- Deterministic by default
- Configurable at runtime or compile time
- Easy to extend with additional policies
- Structured so each cache level can be tested in isolation

## 3. Mandatory Features

### 3.1 Cache Geometry

Each cache level must support:
- Configurable total size in bytes
- Configurable block size in bytes
- Configurable associativity
- Derived set count from `size / (block_size * ways)`
- 32-bit byte-addressed access support

Validation rules:
- Cache size must be non-zero
- Block size must be non-zero
- Associativity must be non-zero
- Cache size must be divisible by block size
- Total block count must be divisible by associativity
- Block size must be a power of two
- Number of sets must be a power of two
- Block size should be a multiple of the modeled word size

### 3.2 Block Metadata

Each cache block must support:
- Tag
- Valid bit
- Dirty bit
- Replacement metadata
- Data payload storage for the modeled block contents

### 3.3 Access Types

The simulator must support:
- Read / load access
- Write / store access

Required behaviors:
- Correct hit detection
- Correct miss handling
- Correct block installation on fill
- Correct replacement on full-set conflict
- Correct update of replacement metadata on access

### 3.4 Write Policies

The simulator must support:
- Write-back
- Write-through

Required semantics:
- Write-back must mark blocks dirty and issue writeback on dirty eviction
- Write-through must propagate writes immediately to the next level

### 3.5 Write Miss Policies

The simulator must support:
- Write-allocate
- No-write-allocate

Required semantics:
- Write-allocate must install a block on write miss before applying the write
- No-write-allocate must forward the write to the next level without installing

### 3.6 Replacement Policies

The simulator must support at minimum:
- LRU
- FIFO
- Random

The design must allow adding more policies later without rewriting core access logic.

### 3.7 Cache Hierarchy

The completed project must support:
- `L1` and `L2` cache levels as the baseline hierarchy
- Future extension path for `L3`
- Communication between levels through a stable lower-level interface

Current implementation status:
- The repository now includes `L1`, `L2`, and `L3` hierarchy support.

Required hierarchy behavior:
- `L1` miss may access `L2`
- `L2` miss may access memory or a modeled backing store
- Dirty eviction from an upper level must be forwarded correctly
- Statistics must identify activity per level

### 3.8 Inclusion Policies

The simulator must support:
- Inclusive
- Exclusive
- Non-inclusive, non-exclusive

Current implementation status:
- The repository currently implements `Inclusive`, `Exclusive`, and
  `Non-inclusive, non-exclusive`.

Current hierarchy policy matrix:
- Supported:
  - `WriteBack + WriteAllocate`
  - `WriteThrough + NoWriteAllocate`
- Rejected:
  - `WriteBack + NoWriteAllocate`
  - `WriteThrough + WriteAllocate`

Required semantics:
- Inclusive: lower level must contain every block present in upper level
- Exclusive: a block should exist in only one cache level at a time
- Non-inclusive: no containment rule is enforced

### 3.9 Statistics

The simulator must collect at minimum:
- Total reads
- Total writes
- Read hits
- Read misses
- Write hits
- Write misses
- Hit rate
- Miss rate
- Clean evictions
- Dirty evictions
- Writebacks

Current implementation status:
- The repository now reports compulsory, capacity, and conflict misses using a
  same-capacity fully associative LRU shadow model for classification.

Per-level statistics are mandatory once `L2` is introduced.

### 3.10 Input and Execution

The simulator must support:
- A simple built-in driver for manual testing
- Trace-driven execution from an input file

Trace support should include:
- Read operations
- Write operations
- Hexadecimal byte addresses
- Optional write values for store operations

Current implementation status:
- The repository now includes a trace runner and regression trace files for scan,
  thrashing, recency-friendly, streaming, and mixed-access patterns.

### 3.11 Correctness Requirements

The simulator must preserve the following invariants:
- No dirty data may be dropped silently
- Address decode must be consistent across all operations
- Replacement metadata must remain valid after every hit, miss, and eviction
- Inclusion policy rules must be enforced when enabled
- Statistics must distinguish read behavior from write behavior
- Write-through and write-back behavior must not be mixed incorrectly

## 4. Optional Advanced Features

These are explicitly out of the mandatory baseline but should remain possible to add later.

### 4.1 Additional Replacement Policies
- MRU
- LFU
- Pseudo-LRU
- NRU
- SRRIP / BRRIP / DRRIP

### 4.2 Timing Model
- Hit latency per level
- Miss penalty per level
- Memory latency
- Average memory access time
- Blocking vs non-blocking cache behavior
- MSHR modeling
- Queueing or contention effects

### 4.3 Prefetching
- Next-line prefetcher
- Stride prefetcher
- Prefetch usefulness tracking
- Prefetch pollution tracking

### 4.4 Coherence and Multi-Core
- Multiple cores
- Snooping coherence
- MESI or MOESI state tracking
- Coherence invalidations and writebacks

### 4.5 Structural Extensions
- Separate `L1I` and `L1D`
- Unified `L2`
- Shared `L3`
- Victim cache
- Sectored cache
- Write buffer
- Banked cache

### 4.6 Miss Classification
- Compulsory misses
- Capacity misses
- Conflict misses
- Coherence misses

## 5. Proposed Functional Model

### 5.1 Addressing

The simulator should use byte addresses externally.

Each cache level should derive:
- Byte offset within a block
- Word offset within a block
- Set index
- Tag

Assumptions:
- Word size is 4 bytes unless explicitly reconfigured
- Accesses in the baseline implementation are word-aligned
- Misaligned accesses may be rejected initially

### 5.2 Read Flow

For a read:
1. Decode tag, index, and offset
2. Search the indexed set
3. On hit:
   - return data
   - update replacement metadata
   - update hit statistics
4. On miss:
   - request the block from the lower level
   - select a victim if needed
   - write back the victim if required by policy
   - install the new block
   - return the requested data
   - update miss statistics

### 5.3 Write Flow

For a write:
1. Decode tag, index, and offset
2. Search the indexed set
3. On hit:
   - update block data
   - update replacement metadata
   - apply write-through or write-back behavior
   - update hit statistics
4. On miss:
   - update miss statistics
   - follow write-allocate or no-write-allocate policy
   - if allocating, fetch/install the block and then apply the write
   - if not allocating, forward the write only

### 5.4 Eviction Flow

On eviction:
- If the victim is invalid, overwrite directly
- If the victim is clean, evict without writeback
- If the victim is dirty, forward a writeback to the lower level
- If inclusion rules require invalidation in another level, perform it before completion

## 6. Class Design

This section defines the recommended design target for the codebase.

### 6.1 `CacheBlock`

Responsibility:
- Represent one cache line and its metadata

Core fields:
- `tag`
- `valid`
- `dirty`
- replacement metadata
- block data

Core methods:
- metadata getters/setters
- reset/clear
- read word
- write word

### 6.2 `CacheSet`

Responsibility:
- Represent one set of blocks
- Encapsulate lookup and victim selection

Core fields:
- collection of `CacheBlock`

Core methods:
- lookup by tag
- find invalid block
- choose victim according to replacement policy

This class is recommended even if the current code keeps sets as a nested vector of block pointers.

Status:
- Implemented in the current repository as a dedicated `CacheSet` abstraction.

### 6.3 `CacheConfig`

Responsibility:
- Hold immutable per-level configuration

Fields:
- level name
- size bytes
- associativity
- write policy
- write miss policy
- replacement policy
- inclusion policy
- latency fields if timing is added

Note:
- In the current implementation direction, block size is a compile-time property
  of the concrete `CacheBlock` and `L1Cache` type, not a runtime field inside `CacheConfig`.

### 6.4 `CacheStats`

Responsibility:
- Hold counters for one cache level

Fields:
- read/write counts
- hit/miss counts
- eviction counts
- writeback counts
- optional latency totals

Methods:
- counter updates
- derived metrics such as hit rate and miss rate

Status:
- Implemented in the current repository as a dedicated `CacheStats` abstraction.

### 6.5 `CacheLevel`

Responsibility:
- Base interface for cache levels and lower-level access targets

Core methods:
- read block
- write block
- read word
- write word
- invalidate
- writeback handling

This should act as the abstraction boundary between `L1`, `L2`, and memory.

### 6.6 `L1Cache`

Responsibility:
- Implement level-1 cache behavior

Must support:
- configurable geometry
- configurable policies
- communication with lower level through the common interface
- statistics collection

### 6.7 `L2Cache`

Responsibility:
- Implement level-2 cache behavior

Same core responsibilities as `L1Cache`, but configured independently.

### 6.8 `MainMemory` or `BackingStore`

Responsibility:
- Act as the terminal lower-level target
- Service block fills and receive writebacks

This may begin as a simple abstract data store rather than a full DRAM timing model.

### 6.9 `TraceRunner`

Responsibility:
- Parse an access trace
- Replay operations into the simulator
- Print results and summary statistics

Status:
- Implemented in the current repository for `R` and `W` trace operations.

## 7. Recommended File Layout

This layout is the target structure for the project as it grows.

```text
Cache-Simulator/
  README.md
  docs/
    spec.md
  include/
    cache_simulator/
      cache.hpp
      cache_block.hpp
      cache_config.hpp
      cache_hierarchy.hpp
      cache_set.hpp
      cache_stats.hpp
      l1_cache.hpp
      replacement_policy.hpp
      trace_runner.hpp
  src/
    main.cpp
    replacement_policy.cpp
  tests/
    test_cache.cpp
  traces/
    sample_trace.txt
  build/
    cache_sim
    cache_tests
```

The repository now follows this layout, with most cache logic remaining header-only
because the main cache classes are templated on block size.

## 8. Configuration Enums

The code should define explicit enums for:
- `WritePolicy`
  - `WriteBack`
  - `WriteThrough`
- `WriteMissPolicy`
  - `WriteAllocate`
  - `NoWriteAllocate`
- `ReplacementPolicy`
  - `LRU`
  - `FIFO`
  - `Random`
- `InclusionPolicy`
  - `Inclusive`
  - `Exclusive`
  - `NonInclusive`

Using enums is preferred over raw booleans once more than one policy is supported.

## 9. Testing Requirements

The project should add unit tests covering:
- Geometry validation
- Address decode
- Read hit behavior
- Read miss behavior
- Write hit behavior
- Write miss behavior
- Write-through semantics
- Write-back semantics
- Write-allocate semantics
- No-write-allocate semantics
- LRU/FIFO/Random victim selection
- Inclusive hierarchy behavior
- Exclusive hierarchy behavior
- Dirty eviction and writeback correctness

Trace-driven tests should cover:
- Reproducible hit/miss counts
- Correct summary statistics
- Correct behavior across multiple cache levels

## 10. Milestone Plan

### Milestone 1: Single-Level Functional L1

Deliverables:
- `CacheBlock`
- `L1Cache`
- Configurable size, block size, associativity
- Read and write support
- Basic statistics
- Compile-clean project

Exit criteria:
- Single-level simulation runs correctly
- Basic hit/miss behavior is demonstrable

### Milestone 2: Policy Cleanup

Deliverables:
- Replace policy booleans with enums
- Implement write-back and write-through cleanly
- Implement write-allocate and no-write-allocate cleanly
- Add replacement policy selection for `LRU`, `FIFO`, `Random`
- Add validation for unsupported combinations if desired

Exit criteria:
- Policy behavior is explicit and testable

### Milestone 3: Lower-Level Interface

Deliverables:
- Introduce a stable lower-level abstraction
- Add `MainMemory` or `BackingStore`
- Remove any remaining hardcoded assumptions tying cache logic to a local demo

Exit criteria:
- `L1` can miss into a lower-level object
- Dirty evictions are forwarded correctly

### Milestone 4: Add L2

Deliverables:
- `L2Cache`
- Configurable `L1 -> L2 -> Memory` chain
- Per-level stats

Exit criteria:
- Misses and writebacks propagate across levels correctly

### Milestone 5: Inclusion Policies

Deliverables:
- Inclusive mode
- Exclusive mode
- Non-inclusive mode

Exit criteria:
- Hierarchy state respects the selected inclusion policy

### Milestone 6: Trace Runner

Deliverables:
- Trace parser
- Replay engine
- Configurable run from input file
- End-of-run statistics summary

Exit criteria:
- Trace files can drive the simulator without code changes

### Milestone 7: Tests and Verification

Deliverables:
- Unit tests for core cache logic
- Integration tests for hierarchy behavior
- Regression tests for policy combinations

Exit criteria:
- Core simulator behavior is covered by automated tests

### Milestone 8: Optional Advanced Extensions

Possible additions:
- `L3`
- latency model
- prefetching
- miss classification
- coherence

Exit criteria:
- Advanced features remain isolated and do not destabilize the core simulator

## 11. Immediate Recommended Next Step

The next implementation step for this repository should be:
- Introduce explicit policy enums and a `CacheConfig` type
- Keep the current `L1Cache` working
- Remove raw boolean policy arguments from the public API

That is the cleanest way to move from a prototype to an extensible simulator without rewriting everything later.
