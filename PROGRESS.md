# Mini-LSM C++ Port — Implementation Progress Log

**Status: COMPLETE ✅**  
All 37 tests pass. ASan clean. TSan clean. CLI operational.

---

## Final Test Results

| Build Type | Tests Passed | Tests Failed | Notes |
|------------|-------------|--------------|-------|
| Standard (`build/`) | 37/37 | 0 | Release build |
| ASan (`build-asan/`) | 37/37 | 0 | Zero memory errors |
| TSan (`build-tsan/`) | 37/37 | 0 | Zero data races |

---

## Full Implementation Progress (§6 of IRONCLAD PLAN)

### Phase 1 — Build System & Core Data Types

| Step | Component | Status | What was built |
|------|-----------|--------|----------------|
| Step 1 | `CMakeLists.txt` | ✅ | CMake 3.20+ project, C++17, FetchContent for GoogleTest/nlohmann\_json/farmhash, mutually exclusive ASAN/TSAN options, `add_mini_lsm_test()` helper |
| Step 2 | `key.h` / `key.cpp` | ✅ | `KeySlice` (non-owning string\_view-like), `KeyBytes` (owned), comparison operators, `to_string()` helper |
| Step 3 | `endian.h` | ✅ | `put_u16_be`, `get_u16_be`, `put_u32_be`, `get_u32_be` for portable big-endian encoding |

### Phase 2 — Storage Primitives

| Step | Component | Status | What was built |
|------|-----------|--------|----------------|
| Step 4 | `block/builder.cpp` + `block.h` | ✅ | Prefix key compression, offset array, `build()` returns raw bytes |
| Step 5 | `block/iterator.cpp` | ✅ | Binary seek on block index, prefix-aware key decode, `next()` |
| Step 6 | `block_cache.cpp` | ✅ | LRU eviction (doubly-linked list + hash map), MRU bump on hit, configurable capacity |
| Step 7 | `table/bloom.cpp` | ✅ | Double hashing (FNV-like), bit array, `may_contain()`, FPR < 2% verified |
| Step 8 | `table/builder.cpp` | ✅ | Multi-block SSTable construction, Bloom filter integration, CRC32 block checksums |
| Step 9 | `table/iterator.cpp` | ✅ | Binary seek to block, within-block iteration, `seek_to_key()` |

### Phase 3 — MemTable, WAL, Iterators

| Step | Plan §6 Step | Component | Status | What was built |
|------|-------------|-----------|--------|----------------|
| — | Step 10 | `wal.cpp` | ✅ | Length-prefixed records + CRC32, `put()`, `sync()`, recovery (truncates at first bad CRC), `create_with_wal()` / `recover_from_wal()` on MemTable |
| — | Step 11 | `mem_table.cpp` | ✅ | `std::map` under `shared_mutex`, concurrent 10-writer/10-reader test, `flush()` to SSTable, `scan()` returning `MemTableIterator` snapshot |
| — | Step 12 | `iterators.h` | ✅ | `StorageIterator` CRTP base, `Bound` (Included/Excluded/Unbounded) |
| — | Step 13 | `merge_iterator.cpp` | ✅ | Min-heap over N iterators, deduplication by key (first wins), tombstone passthrough |
| — | Step 14 | `two_merge_iterator.cpp` | ✅ | Combines A (newer) and B (older) iterators, A shadows B on equal key |
| — | Step 15 | `concat_iterator.cpp` | ✅ | Sequential concat of non-overlapping SSTables (L1+), binary SST selection on seek |

### Phase 4 — LSM Iterator, Manifest, Compaction Strategies

| Step | Component | Commit | Status | What was built |
|------|-----------|--------|--------|----------------|
| Step 16 | `lsm_iterator.cpp` | `19c9c02` | ✅ | Tombstone skipping, upper-bound enforcement, `FusedIterator` wraps `LsmIterator` to stop on invalid |
| Step 17 | `manifest.cpp` | `e47030c` | ✅ | JSON + CRC32 per record, `add_record()`, `add_record_when_init()`, `recover()` → vector of `ManifestRecord` |
| Step 18 | `compact/simple_leveled.cpp` | `7d48cff` | ✅ | L0 trigger (count threshold), level size-ratio trigger, `apply_compaction_result()` |
| Step 19 | `compact/tiered.cpp` | `8b3b367` | ✅ | Space amplification trigger, size-ratio trigger, tier grouping, `apply_compaction_result()` |
| Step 20 | `compact/leveled.cpp` | `e7db415` | ✅ | L0 trigger, target-size-based level selection, `apply_compaction_result()` with `in_recovery` support |

### Phase 5 — Compaction Engine, Storage Wiring, CLI

| Step | Component | Commit | Status | What was built |
|------|-----------|--------|--------|----------------|
| Step 21 | `compact.cpp` | `833e76a` | ✅ | `compact()` builds SSTs from merged iterators; `apply_compaction_result()` dispatches to strategy + handles `ForceFull`; background flush thread; background compaction thread; `force_full_compaction()` |
| Step 22 | `lsm_storage.cpp` | `833e76a` | ✅ | `open()` with full Manifest + WAL recovery; `get()`, `put()`, `del()`, `scan()` with full iterator stack; `sync()`; `force_freeze_memtable()` with WAL-backed MemTable creation; `force_flush_next_imm_memtable()`; `MiniLsm::close()` |
| Step 23 | `bin/mini_lsm_cli.cpp` | `833e76a` | ✅ | Interactive REPL: `open`, `put`, `get`, `delete`, `scan`, `flush`, `compact`, `quit`; scripted smoke test |
| Step 24 | Full Suite | `833e76a` | ✅ | 37/37 tests pass under Standard, ASan, TSan |

---

## Test Suite Breakdown (37 Tests)

| Test Binary | Tests | Covers |
|-------------|-------|--------|
| `test_block` | 4 | BlockBuilder prefix compression, BlockIterator seek, shared prefix edge cases, endian encoding, CRC32 |
| `test_block_cache` | 1 | LRU eviction, MRU bump on hit |
| `test_mem_table` | 3 | Concurrent 10-writer/10-reader, flush→SSTable round trip, range scan boundary conditions |
| `test_table` | 2 | Multi-block SSTable layout + checksums, end-to-end seek + Bloom rejection |
| `test_bloom` | 1 | False positive rate < 2% over 10,000 keys |
| `test_iterators` | 5 | MergeIterator overlapping+shadows, TwoMergeIterator shadowing, SstConcatIterator 3-SST seek, LsmIterator tombstone skip + bounds, FusedIterator stop-at-invalid |
| `test_wal` | 2 | Corrupt-last-5-bytes → recover 99/100 records; crash (no clean close) → recover all records |
| `test_manifest` | 2 | Full round-trip NewMemtable/Flush/Compaction records; corrupt record stops replay gracefully |
| `test_simple_leveled` | 3 | L0 trigger, size-ratio trigger, no-trigger |
| `test_tiered` | 3 | Space amplification trigger, size-ratio trigger, insufficient tiers |
| `test_leveled` | 2 | L0 trigger, no-trigger |
| `test_lsm_storage` | 3 | Basic put/get/delete, scan with bounds, freeze + flush lifecycle |
| `test_compact` | 3 | End-to-end SimpleLeveled compaction, ForceFullCompaction with tombstone drop, background thread execution |
| `test_integration` | 1 | 1000 keys: write → freeze → flush → compact → close → reopen → verify all survive |

---

## Known Architectural Notes

### `LsmStorageState` is copy-on-write
Every mutation (freeze, flush, compaction apply) creates a new `shared_ptr<LsmStorageState>`. Readers take a `shared_lock` to snapshot the pointer; they hold the old state alive while compaction runs. No reader ever blocks a writer.

### WAL lifecycle
- `open()` with WAL disabled → plain `std::make_shared<MemTable>(id)`.
- `open()` with WAL enabled → `MemTable::create_with_wal(id, path)`.
- `force_freeze_memtable()` respects `options.enable_wal` and creates the replacement MemTable with a fresh WAL accordingly.
- `force_flush_next_imm_memtable()` builds an SSTable, records a `Flush` Manifest record, then deletes the WAL file.

### Compaction controller dispatch
`CompactionController::apply_compaction_result()` first checks if the task is `ForceFull` (handled inline, controller-agnostic), then dispatches to the strategy-specific impl. This allows `force_full_compaction()` to work with `NoCompaction` controller without throwing.

### Release Build Integrity (`assert` behavior)
In Release builds (`-DCMAKE_BUILD_TYPE=Release`), `NDEBUG` is defined by CMake. Any state-mutating method calls (like `builder_.add()`) placed inside `assert(...)` are stripped by the preprocessor. All mutations are performed explicitly before any assertions (`bool added = builder_.add(...); assert(added);`).

### Recovery sequence in `LsmStorageInner::open()`
1. Check if MANIFEST exists.
2. If not → fresh DB, create Manifest + WAL-backed MemTable, write `NewMemtable` record.
3. If yes → replay all Manifest records to reconstruct `LsmStorageState`:
   - `NewMemtable` → add ID to pending memtable set
   - `Flush` → remove from pending set, prepend SST ID to L0 (or new tier)
   - `Compaction` → `apply_compaction_result()` to update level structure
4. Open all SST files referenced in the final state.
5. Recover WAL files for all memtable IDs still pending (not yet flushed).
6. Create a new active MemTable (with WAL) and write its `NewMemtable` record.
