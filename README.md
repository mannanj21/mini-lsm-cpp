# Mini-LSM C++ — Log-Structured Merge Tree Storage Engine

[![CI](https://github.com/mannanj21/mini-lsm-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/mannanj21/mini-lsm-cpp/actions/workflows/ci.yml)

A complete C++17 implementation of a Log-Structured Merge Tree (LSM-Tree) storage engine, ported from the [Mini-LSM](https://github.com/skyzh/mini-lsm) Rust tutorial series by Alex Chi Z.

**37 tests · AddressSanitizer clean · ThreadSanitizer clean**

---

## Table of Contents

- [What is an LSM Tree?](#what-is-an-lsm-tree)
- [Features](#features)
- [Architecture Overview](#architecture-overview)
- [Project Structure](#project-structure)
- [Prerequisites](#prerequisites)
- [Building](#building)
- [Running Tests](#running-tests)
- [Using the CLI](#using-the-cli)
- [Key Design Decisions](#key-design-decisions)
- [Component Deep-Dive](#component-deep-dive)

---

## What is an LSM Tree?

An LSM Tree is a data structure optimized for **write-heavy workloads**. Instead of writing directly to a sorted on-disk structure (like a B-Tree), all writes first go to an in-memory buffer (MemTable). When the buffer fills, it is flushed to an immutable on-disk file (SSTable). Over time, multiple SSTables accumulate and are periodically merged (compacted) to reclaim space and maintain read performance.

```
Write Path:
  put(k, v) → WAL → MemTable → [freeze] → ImmMemTable → [flush] → SSTable (L0)
                                                                       ↓ compaction
                                                                   SSTable (L1, L2...)

Read Path:
  get(k) → MemTable → ImmMemTables → L0 SSTables → L1+ SSTables (bloom filter skip)
```

---

## Features

| Feature | Status |
|---------|--------|
| MemTable with concurrent reads/writes (`shared_mutex`) | ✅ |
| Write-Ahead Log (WAL) with CRC32 integrity + crash recovery | ✅ |
| SSTable builder with prefix key compression | ✅ |
| Bloom filters (false positive rate < 2%) | ✅ |
| Block cache (LRU, configurable size) | ✅ |
| Manifest log for persistent LSM state | ✅ |
| Iterator stack: Merge, TwoMerge, SstConcat, Lsm, Fused | ✅ |
| Tombstone deletion with compaction GC | ✅ |
| Compaction: NoCompaction, SimpleLeveled, Tiered, Leveled | ✅ |
| Background flush & compaction threads | ✅ |
| Graceful `close()` with WAL sync | ✅ |
| Full DB recovery on `open()` from WAL + Manifest | ✅ |
| CLI REPL (`mini_lsm_cli`) | ✅ |
| AddressSanitizer (ASan) clean | ✅ |
| ThreadSanitizer (TSan) clean | ✅ |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────┐
│                   MiniLsm                        │
│  (flush thread + compaction thread + close())    │
├─────────────────────────────────────────────────┤
│               LsmStorageInner                    │
│   get / put / del / scan / force_freeze /        │
│   force_flush / open (recovery)                  │
├──────────────┬──────────────────────────────────┤
│  MemTable    │       SSTable Files (disk)        │
│  (skiplist)  │   L0: [sst0, sst1, sst2, ...]    │
│  + WAL       │   L1: [sst3, sst4, ...]           │
│              │   L2: ...                         │
│  ImmMemTable │                                   │
│  list        │   Each SST:                       │
│              │   ┌──────────────────────────┐    │
│              │   │  Blocks (prefix encoded) │    │
│              │   │  Block index             │    │
│              │   │  Bloom filter            │    │
│              │   │  Metadata + CRC32        │    │
│              │   └──────────────────────────┘    │
├──────────────┴──────────────────────────────────┤
│              Manifest (JSON + CRC32)             │
│  Tracks: NewMemtable / Flush / Compaction events │
└─────────────────────────────────────────────────┘
```

---

## Project Structure

```
mini-lsm-cpp/
├── CMakeLists.txt              # Build system
├── README.md
├── PROGRESS.md                 # Step-by-step implementation log
│
├── include/mini_lsm/           # Public headers
│   ├── key.h                   # KeySlice (non-owning), KeyBytes (owned)
│   ├── block.h                 # Block builder + data layout
│   ├── block_cache.h           # LRU block cache
│   ├── table.h                 # SsTable, SsTableBuilder, SsTableIterator
│   ├── mem_table.h             # MemTable + factory methods
│   ├── wal.h                   # Write-Ahead Log
│   ├── manifest.h              # Manifest log
│   ├── iterators.h             # StorageIterator base + Bound types
│   ├── merge_iterator.h        # MergeIterator (heap-based)
│   ├── two_merge_iterator.h    # TwoMergeIterator (A over B)
│   ├── concat_iterator.h       # SstConcatIterator (non-overlapping L1+)
│   ├── lsm_iterator.h          # LsmIterator (tombstone skip + bounds)
│   ├── compact.h               # Compaction tasks, controllers, options
│   └── lsm_storage.h           # LsmStorageState, LsmStorageInner, MiniLsm
│
├── src/
│   ├── key.cpp
│   ├── block/
│   │   ├── builder.cpp         # Block encoding (prefix compression)
│   │   └── iterator.cpp        # Block decoding + seek
│   ├── block_cache.cpp
│   ├── table/
│   │   ├── builder.cpp         # SSTable construction
│   │   ├── iterator.cpp        # SSTable scan + seek
│   │   └── bloom.cpp           # Bloom filter (double hashing)
│   ├── iterators/
│   │   ├── mem_table_iterator.cpp
│   │   ├── merge_iterator.cpp
│   │   ├── two_merge_iterator.cpp
│   │   └── concat_iterator.cpp
│   ├── compact/
│   │   ├── simple_leveled.cpp  # SimpleLeveled strategy
│   │   ├── tiered.cpp          # Tiered strategy
│   │   └── leveled.cpp         # Leveled strategy
│   ├── mem_table.cpp
│   ├── wal.cpp
│   ├── manifest.cpp
│   ├── lsm_iterator.cpp
│   ├── lsm_storage.cpp         # Core engine: open/get/put/scan/close
│   └── compact.cpp             # Compaction execution + background threads
│
├── bin/
│   └── mini_lsm_cli.cpp        # Interactive REPL
│
└── tests/
    ├── test_block.cpp
    ├── test_block_cache.cpp
    ├── test_mem_table.cpp
    ├── test_table.cpp
    ├── test_bloom.cpp
    ├── test_iterators.cpp
    ├── test_wal.cpp
    ├── test_manifest.cpp
    ├── test_simple_leveled.cpp
    ├── test_tiered.cpp
    ├── test_leveled.cpp
    ├── test_lsm_storage.cpp
    ├── test_compact.cpp
    ├── test_integration.cpp
    └── smoke_test_cli.sh
```

---

## Prerequisites

- **CMake** ≥ 3.20
- **GCC** ≥ 11 or **Clang** ≥ 13 (C++17 required)
- **ZLIB** (for CRC32: `sudo apt install zlib1g-dev`)
- **Internet access** (first build fetches GoogleTest, nlohmann/json, farmhash via CMake FetchContent)

---

## Building

### Standard build

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
```

### With AddressSanitizer

```bash
cmake -B build-asan -S . -DENABLE_ASAN=ON
cmake --build build-asan -j$(nproc)
```

### With ThreadSanitizer

```bash
cmake -B build-tsan -S . -DENABLE_TSAN=ON
cmake --build build-tsan -j$(nproc)
```

> ASan and TSan are mutually exclusive. Enabling both causes a CMake error by design.

---

## Running Tests

### All tests (standard build)

```bash
ctest --test-dir build --output-on-failure
```

### All tests under ASan

```bash
ctest --test-dir build-asan --output-on-failure
```

### All tests under TSan

```bash
ctest --test-dir build-tsan --output-on-failure
```

### Single test binary

```bash
./build/test_block
./build/test_mem_table
./build/test_wal
./build/test_integration
# etc.
```

### Filter a specific test

```bash
./build/test_iterators --gtest_filter="MergeIteratorTest.*"
```

### CLI smoke test

```bash
./tests/smoke_test_cli.sh
```

### Expected output (all passing)

```
100% tests passed, 0 tests failed out of 37
Total Test time (real) =   0.59 sec
```

---

## Using the CLI

Start the REPL:

```bash
./build/mini_lsm_cli
```

Available commands:

| Command | Description |
|---------|-------------|
| `open <path>` | Open (or create) a database at the given directory |
| `put <key> <value>` | Insert or overwrite a key |
| `get <key>` | Retrieve a value (`(not found)` if absent or deleted) |
| `delete <key>` | Delete a key (write tombstone) |
| `scan <lower> <upper>` | Scan a key range. Use `*` for unbounded. |
| `flush` | Force flush memtable to SSTable on disk |
| `compact` | Force full L0→L1 compaction |
| `quit` / `exit` | Graceful shutdown and exit |

### Example session

```
open /tmp/mydb
OK
put hello world
OK
put foo bar
OK
get hello
world
get missing
(not found)
scan * *
foo bar
hello world
OK
delete foo
OK
flush
OK
compact
OK
quit
```

Re-open the same path — all data survives:

```
open /tmp/mydb
get hello
world
get foo
(not found)
```

---

## Key Design Decisions

### 1. Immutable State Snapshots (`std::shared_ptr<LsmStorageState>`)
All state mutations create a new `shared_ptr<LsmStorageState>` rather than mutating in-place. Readers snapshot the pointer under a `shared_lock`, so reads never block writes and vice-versa.

### 2. WAL Before MemTable
Every `put` / `del` is written to the WAL before updating the MemTable. On crash, the WAL replays any unflushed records. On clean close, the WAL file is deleted after its MemTable is flushed to SSTable.

### 3. Manifest Log
The Manifest records three event types: `NewMemtable`, `Flush`, `Compaction`. On `open()`, the engine replays all Manifest records to reconstruct `LsmStorageState`, then opens the surviving SST files. This means zero data loss across clean restarts.

### 4. Bloom Filters Skip Reads
Each SSTable has a Bloom filter (FPR < 2%). `get()` on L0/L1 SSTables skips the full block read if the Bloom filter reports absent.

### 5. Iterator Stack
Reads use a layered iterator stack:
```
FusedIterator
  └── LsmIterator (tombstone skip + upper bound)
        └── TwoMergeIterator
              ├── A: MergeIterator<MemTableIterator>  (MemTable + all ImmMemTables)
              └── B: TwoMergeIterator
                    ├── A: MergeIterator<SsTableIterator>  (L0, unsorted)
                    └── B: SstConcatIterator               (L1+, sorted)
```

---

## Component Deep-Dive

### Block
The smallest unit of storage. Keys are prefix-compressed within a block. Each block ends with a CRC32 checksum. The block index stores the first key of each block for binary search.

### SSTable (`.sst`)
An immutable on-disk file containing:
- One or more Blocks
- A Block index (first key per block + file offset)
- A Bloom filter serialized at the end
- Metadata footer with checksum

### MemTable
An in-memory `std::map<std::string, std::string>` guarded by `std::shared_mutex`. Supports concurrent reads via `shared_lock` and exclusive writes via `unique_lock`. Backed by an optional WAL file.

### WAL
A sequential append-only file. Each record: `[key_len:2][key][val_len:2][val][crc32:4]`. Recovery reads until a CRC mismatch or EOF, discarding the last partial record (crash-safe).

### Manifest
A JSON-lines file. Each line: `{"type": "...", ...}` followed by a CRC32. The engine replays all valid records on startup. Corrupt records stop replay (rather than crashing).

### Compaction

| Strategy | When to use |
|----------|-------------|
| `NoCompaction` | Testing only. Manual `force_full_compaction()` only. |
| `SimpleLeveled` | Simple size-ratio trigger between adjacent levels. |
| `Tiered` | Write-optimized. Groups SSTables into tiers by size. |
| `Leveled` | Space-efficient. Each level is fully sorted. |

---

## Engineering Notes

Real bugs found and fixed during implementation — the kind of issues that only surface when you actually build the whole system end-to-end.

### Bug 1 — `apply_compaction_result` incorrectly threw on `ForceFull` tasks

`CompactionController::apply_compaction_result()` dispatched by controller type (SimpleLeveled / Tiered / Leveled / NoCompaction). The issue: `ForceFullCompaction` tasks are **strategy-agnostic** — they don't belong to any controller. The function threw `"NoCompaction controller cannot apply result"` even when the controller was `NoCompaction` and the task was a perfectly valid force-full compaction.

The fix: check `task.type == ForceFull` **before** the controller switch and handle it inline — remove the input SSTables, replace L1 with the output SSTables, filter L0 accordingly. The controller never needs to know about it.

**Why this matters:** This is exactly the class of bug that emerges at integration time when your compaction controller and your compaction executor are designed independently. The controller's job is to *decide when and what* to compact; the executor's job is to *do it and update state*. Mixing those responsibilities causes subtle crashes that only appear on `force_full_compaction()` calls — which are exactly what you'd call in tests.

### Bug 2 — WAL-less MemTable created after freeze when WAL was enabled

`force_freeze_memtable()` created the **replacement** active MemTable via `std::make_shared<MemTable>(id)` regardless of `options.enable_wal`. Consequence: if the DB was opened with WAL enabled, the MemTable was frozen (moved to the immutable list), and a new plain MemTable was created — **without a WAL file**. Any subsequent writes to that MemTable had no crash durability. On restart, those writes would be silently lost.

The fix: `force_freeze_memtable()` respects `options.enable_wal` and calls `MemTable::create_with_wal(id, path_of_wal(id))` for the replacement. The test that caught it: the full-lifecycle integration test (`FullLifecycle1000KeysSurvival`) — it writes, freezes multiple times, closes, reopens, and verifies every key survived.

**Why this matters:** This is the exact failure mode that makes crash recovery implementations unreliable in practice. The WAL exists for durability; if you forget to attach it to newly created MemTables after a freeze, you have a silent hole that doesn't show up until you simulate a crash *between* a freeze and a flush.

---

## License

This project is a personal educational implementation. The original Mini-LSM tutorial is by [Alex Chi Z](https://github.com/skyzh/mini-lsm) under the Apache 2.0 License.
