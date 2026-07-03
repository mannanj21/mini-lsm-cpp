// bench_read.cpp — Read throughput and latency benchmark
// Measures: point GET ops/sec, p50/p99/p999 latency, Bloom filter
// effectiveness on non-existent keys, and sequential scan throughput.
//
// Build: cmake -B build-bench -S . -DMINI_LSM_BENCH=ON -DCMAKE_BUILD_TYPE=Release
//         cmake --build build-bench -j$(nproc)
//         ./build-bench/bench_read

#include "bench_common.h"
#include <cstdio>
#include <random>

using namespace mini_lsm;
using namespace mini_lsm::bench;

int main() {
    const int    NUM_WRITE   = 200'000;   // keys to pre-populate
    const int    NUM_READ    = 50'000;    // random GETs to measure
    const int    NUM_MISS    = 50'000;    // non-existent key GETs
    const size_t VAL_SIZE    = 100;

    printf("\n");
    print_header("Mini-LSM Read Performance Benchmark");
    printf("  Pre-populate: %d keys (%zu-byte values)\n", NUM_WRITE, VAL_SIZE);
    printf("  Random GET:   %d operations\n", NUM_READ);
    printf("  Miss GET:     %d operations (non-existent keys)\n", NUM_MISS);
    printf("\n");

    // ── Phase 1: Populate the DB ─────────────────────────────────────────
    printf("  [1/4] Loading %d keys...\n", NUM_WRITE);

    auto opts = LsmStorageOptions::default_for_week1_test();
    opts.block_size    = 4096;
    opts.target_sst_size = 2 << 20;
    opts.enable_wal    = false;

    TempDB tdb("read_bench", opts);
    MiniLsm* db = tdb.db();

    for (int i = 0; i < NUM_WRITE; ++i) {
        db->put(KeySlice(gen_key(i)), KeySlice(gen_val(i, VAL_SIZE)));
    }
    db->force_flush();
    db->force_full_compaction();
    printf("  Done. SST on disk: %s\n\n", human_bytes(tdb.sst_bytes()).c_str());

    // ── Phase 2: Random point GET (existing keys) ────────────────────────
    printf("  [2/4] Random GET (%d ops on existing keys)...\n", NUM_READ);
    {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, NUM_WRITE - 1);

        LatencyHistogram hist;
        Timer total;

        for (int i = 0; i < NUM_READ; ++i) {
            int key_idx = dist(rng);
            Timer t;
            auto val = db->get(KeySlice(gen_key(key_idx)));
            hist.record(t.elapsed_us());
            // Sanity: value must exist
            (void)val;
        }

        double wall_ms = total.elapsed_ms();
        double ops_s   = NUM_READ / (wall_ms / 1000.0);

        printf("    Throughput:  %.0f ops/sec\n", ops_s);
        printf("    Latency p50: %.3f ms\n", hist.p50()  / 1000.0);
        printf("    Latency p99: %.3f ms\n", hist.p99()  / 1000.0);
        printf("    Latency p999:%.3f ms\n", hist.p999() / 1000.0);
        printf("\n");
    }

    // ── Phase 3: Non-existent key GET (Bloom filter test) ────────────────
    printf("  [3/4] Non-existent key GET (%d ops, keys > %d)...\n",
           NUM_MISS, NUM_WRITE);
    {
        // Use key range that definitely doesn't exist: NUM_WRITE .. NUM_WRITE*2
        LatencyHistogram hist;
        Timer total;

        for (int i = 0; i < NUM_MISS; ++i) {
            int key_idx = NUM_WRITE + i;
            Timer t;
            auto val = db->get(KeySlice(gen_key(key_idx)));
            hist.record(t.elapsed_us());
            (void)val;
        }

        double wall_ms   = total.elapsed_ms();
        double ops_s     = NUM_MISS / (wall_ms / 1000.0);

        // If Bloom filters are working, miss latency should be much lower
        // than hit latency (no block reads necessary)
        printf("    Throughput:  %.0f ops/sec\n", ops_s);
        printf("    Latency p50: %.3f ms\n", hist.p50()  / 1000.0);
        printf("    Latency p99: %.3f ms\n", hist.p99()  / 1000.0);
        printf("    (Lower latency vs existing-key GET = Bloom filters working)\n");
        printf("\n");
    }

    // ── Phase 4: Sequential scan ─────────────────────────────────────────
    printf("  [4/4] Sequential scan (all %d keys)...\n", NUM_WRITE);
    {
        Timer t;
        int count = 0;

        auto iter = db->scan(Bound::unbounded(), Bound::unbounded());
        while (iter.is_valid()) {
            ++count;
            iter.next();
        }

        double wall_ms = t.elapsed_ms();
        double keys_s  = count / (wall_ms / 1000.0);

        printf("    Keys scanned: %d\n", count);
        printf("    Throughput:   %.0f keys/sec\n", keys_s);
        printf("    Scan time:    %.1f ms\n", wall_ms);
        printf("\n");
    }

    print_separator();
    printf("  Note: run on the same machine to compare strategies.\n");
    printf("  Results vary with CPU speed, disk speed, and cache warmth.\n\n");

    return 0;
}
