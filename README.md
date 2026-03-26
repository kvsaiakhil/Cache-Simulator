# Cache-Simulator

Step 1 refactor complete:
- DRAM model removed from the simulator core.
- Cache code split into interface and implementation files.
- Two core classes:
  - `CacheBlock`
  - `L1Cache`

Current modular structure:
- `CacheBlockBase` -> base interface for cache block implementations
- `CacheBlock<BlockSizeBytes>` -> concrete cache line implementation with compile-time block size
- `CacheConfig` -> cache geometry and policy configuration
- `HierarchyConfig` -> L1/L2/L3 hierarchy configuration and inclusion policy
- `CacheStats` -> standalone statistics container
- `CacheSet<BlockSizeBytes>` -> standalone set abstraction for lookup and replacement
- `ReplacementPolicy` -> base interface for replacement policies
- `LruReplacementPolicy` -> concrete LRU policy
- `FifoReplacementPolicy` -> concrete FIFO policy
- `RandomReplacementPolicy` -> concrete deterministic-random policy for reproducible tests
- `CacheBase` -> base interface for cache levels
- `L1Cache<BlockSizeBytes>` -> concrete L1 cache implementation with compile-time block size
- `L2Cache<BlockSizeBytes>` -> alias over the same cache-level implementation
- `L3Cache<BlockSizeBytes>` -> alias over the same cache-level implementation
- `CacheHierarchy<BlockSizeBytes>` -> three-level hierarchy wrapper with inclusion-policy control
- `TraceRunner<BlockSizeBytes>` -> trace file parser and replay runner

Current miss reporting:
- read hits / misses
- write hits / misses
- writebacks
- compulsory misses
- capacity misses
- conflict misses

## Specification

Project requirements and implementation roadmap:
- `requirements/spec.md`

## Build and run

```bash
make
./cache_sim
```

## Run a trace

```bash
./cache_sim traces/sample_trace.txt inclusive
./cache_sim traces/sample_trace.txt exclusive
./cache_sim traces/sample_trace.txt non-inclusive
./cache_sim traces/sample_trace.txt inclusive write-back
./cache_sim traces/sample_trace.txt inclusive write-through
./cache_sim traces/sample_trace.txt inclusive csv
./cache_sim traces/sample_trace.txt inclusive json
./cache_sim traces/sample_trace.txt inclusive write-through json
```

Trace format:
- `R <address>`
- `W <address> <value>`

Examples:
- `R 0x10`
- `W 0x20 0x12345678`

SRRIP-inspired regression traces:
- `traces/scan_trace.txt`
- `traces/thrashing_trace.txt`
- `traces/recency_friendly_trace.txt`
- `traces/streaming_trace.txt`
- `traces/mixed_access_pattern_trace.txt`

Miss classification model:
- `compulsory`: first time a block is ever touched
- `conflict`: missed in the real cache but would hit in a same-capacity fully associative LRU shadow cache
- `capacity`: missed in both the real cache and the same-capacity fully associative LRU shadow cache

Hierarchy notes:
- The project now supports `L1`, `L2`, and `L3`
- Inclusion-policy knob:
  - `inclusive`
  - `exclusive`
  - `non-inclusive`
- Supported write-policy pairs across the whole hierarchy:
  - `WriteBack + WriteAllocate`
  - `WriteThrough + NoWriteAllocate`
- CLI write-policy knob:
  - `write-back` or `wb`
  - `write-through` or `wt`
- The same write policy and write-miss policy must be used on `L1`, `L2`, and `L3`
- Current hierarchy implementation assumes the same block size across all levels
- Stats export formats:
  - `csv`
  - `json`

## Tests

```bash
make test
```
