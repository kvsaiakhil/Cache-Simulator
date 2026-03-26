# UI Frontend

This directory contains a standalone browser frontend for the cache simulator.

## What it does

- provides a modern visual dashboard for the simulator
- replays built-in traces step by step
- shows live `L1`, optional `VC`, `L2`, and `L3` contents
- lets you switch:
  - hierarchy mode
  - write mode
  - replacement policy
  - victim-cache size and policy
- includes live CSV and JSON stats export panels
- supports play, pause, step, reset, and trace editing
- includes tiny, medium, and long built-in trace presets

## How to run it

You can either open [index.html](index.html) directly in a browser or serve the directory with a static file server.

Example:

```bash
cd ui
python3 -m http.server 8080
```

Then open:

```text
http://localhost:8080
```

## Important note

This frontend mirrors the simulator behavior in JavaScript for visualization, including hierarchy mode, supported write modes, replacement policy, victim cache, and live stats export. It is not yet wired directly to the C++ binary as a live backend service.
