# Trace Index

This directory contains the trace corpus used by the cache simulator.

The traces are organized by size:

- `tiny`
  Original small regression traces
- `medium`
  1000 memory accesses per trace
- `long`
  10000 memory accesses per trace

Each size bucket contains the same behavior families so you can compare how a policy behaves under short, medium, and long runs without changing the qualitative access pattern.

## Trace Families

| Trace | Behavior | Tiny | Medium | Long |
| --- | --- | ---: | ---: | ---: |
| `sample_trace.txt` | Small mixed read/write locality demo | 5 | 1000 | 10000 |
| `scan_trace.txt` | Two-pass scan over a working set larger than the cache | 16 | 1000 | 10000 |
| `thrashing_trace.txt` | Three lines repeatedly mapping to the same set in a 2-way cache | 6 | 1000 | 10000 |
| `recency_friendly_trace.txt` | Small working set repeatedly reused | 12 | 1000 | 10000 |
| `streaming_trace.txt` | One-pass stream with no reuse | 12 | 1000 | 10000 |
| `mixed_access_pattern_trace.txt` | Hot reused lines plus a perturbing stream | 13 | 1000 | 10000 |

## Directory Layout

```text
traces/
  tiny/
    sample_trace.txt
    scan_trace.txt
    thrashing_trace.txt
    recency_friendly_trace.txt
    streaming_trace.txt
    mixed_access_pattern_trace.txt
  medium/
    sample_trace.txt
    scan_trace.txt
    thrashing_trace.txt
    recency_friendly_trace.txt
    streaming_trace.txt
    mixed_access_pattern_trace.txt
  long/
    sample_trace.txt
    scan_trace.txt
    thrashing_trace.txt
    recency_friendly_trace.txt
    streaming_trace.txt
    mixed_access_pattern_trace.txt
```

## Notes

- `medium` and `long` traces are not arbitrary expansions. They preserve the same intended behavior as the original `tiny` traces.
- The regression tests currently use the `tiny` traces for stable exact-count assertions.
