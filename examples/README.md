# Example Runs

This directory contains real output captured from the simulator so users can see the expected CLI formats before running their own traces.

## What is included

- [plain_text_summary.txt](plain_text_summary.txt)
  Standard human-readable trace replay plus the per-level summary.
- [csv_export.txt](csv_export.txt)
  Human-readable summary followed by CSV stats export.
- [json_export.txt](json_export.txt)
  Human-readable summary followed by JSON stats export.
- [victim_cache_summary.txt](victim_cache_summary.txt)
  Output shape when the optional victim cache is enabled.
- [write_through_summary.txt](write_through_summary.txt)
  Output shape for `WriteThrough + NoWriteAllocate`.

## How to read the output

Each run has three parts:

1. Per-access trace replay
   Each line shows the operation, the address, the value observed or written, and whether the top-level access was a hit or miss.

2. Operation totals
   The `operations=`, `loads=`, and `stores=` line summarizes the replayed trace.

3. Per-level statistics
   The simulator prints one line per cache level:
   - `L1`
   - optional `VC`
   - `L2`
   - `L3`

## Meaning of the summary fields

- `read_hits`
  Load requests satisfied by that level.
- `read_misses`
  Load requests that missed in that level.
- `write_hits`
  Store requests satisfied by that level.
- `write_misses`
  Store requests that missed in that level.
- `writebacks`
  Dirty evictions written to the next level or backing memory.
- `compulsory_misses`
  First touch of a block. Reported on `L1`.
- `capacity_misses`
  Misses caused by total cache capacity pressure. Reported on `L1`.
- `conflict_misses`
  Misses caused by set conflicts. Reported on `L1`.

## Representative commands

These files were generated from these commands:

```bash
./build/cache_sim traces/tiny/sample_trace.txt inclusive
./build/cache_sim traces/tiny/sample_trace.txt inclusive csv
./build/cache_sim traces/tiny/sample_trace.txt inclusive json
./build/cache_sim traces/tiny/thrashing_trace.txt inclusive vc=2:lru
./build/cache_sim traces/tiny/sample_trace.txt inclusive write-through
```

## Sample output excerpt

From [plain_text_summary.txt](plain_text_summary.txt):

```text
W 0x0 <- 287454020 (miss)
R 0x0 -> 287454020 (hit)
R 0x10 -> 0 (miss)
W 0x20 <- 2882400001 (miss)
R 0x20 -> 2882400001 (hit)

operations=5 loads=3 stores=2
L1 read_hits=2 read_misses=1 write_hits=0 write_misses=2 writebacks=0 compulsory_misses=3 capacity_misses=0 conflict_misses=0
L2 read_hits=0 read_misses=3 write_hits=0 write_misses=0 writebacks=0
L3 read_hits=0 read_misses=3 write_hits=0 write_misses=0 writebacks=0
```

## Notes for users

- The CLI always prints the human-readable summary first.
- `csv` appends a CSV block after the summary.
- `json` appends a JSON object after the summary.
- When the victim cache is enabled, an extra `VC` configuration line and `VC` stats line appear.
- Under `write-through`, stores can still be reported as misses even though backing memory is updated correctly, because `NoWriteAllocate` is paired with that mode in this simulator.
