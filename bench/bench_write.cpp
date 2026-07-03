// bench_write.cpp — Write throughput benchmark
// Measures: sequential write ops/sec, MB/s, and write amplification
// across all four compaction strategies.
//
// Build: cmake -B build-bench -S . -DMINI_LSM_BENCH=ON -DCMAKE_BUILD_TYPE=Release
//         cmake --build build-bench -j$(nproc)
//         ./build-bench/bench_write

#include "bench_common.h"
#include "mini_lsm/compact.h"
#include <cstdio>
#include <cstring>

using namespace mini_lsm;
using namespace mini_lsm::bench;

struct WriteResult {
    std::string strategy_name;
    int num_keys;
    size_t val_size;
    double wall_ms;
    double ops_per_sec;
    double mb_per_sec;
    uintmax_t user_bytes;    // bytes the user wrote
    uintmax_t disk_bytes;    // bytes actually on disk (SST files)
    double write_amp;        // disk_bytes / user_bytes
};

WriteResult run_write_bench(
        const std::string& name,
        LsmStorageOptions opts,
        int num_keys,
        size_t val_size) {

    const size_t key_size = gen_key(0).size();
    const uintmax_t user_bytes = (uintmax_t)num_keys * (key_size + val_size);

    TempDB tdb(name, opts);
    MiniLsm* db = tdb.db();

    Timer t;
    for (int i = 0; i < num_keys; ++i) {
        db->put(KeySlice(gen_key(i)), KeySlice(gen_val(i, val_size)));
    }
    // Flush all remaining data to disk so we can measure SST file sizes
    db->force_flush();
    double wall_ms = t.elapsed_ms();

    uintmax_t disk_bytes = tdb.sst_bytes();
    double ops_per_sec  = num_keys / (wall_ms / 1000.0);
    double mb_per_sec   = (user_bytes / (1024.0 * 1024.0)) / (wall_ms / 1000.0);
    double write_amp    = disk_bytes > 0 ? (double)disk_bytes / user_bytes : 1.0;

    return {name, num_keys, val_size, wall_ms, ops_per_sec, mb_per_sec,
            user_bytes, disk_bytes, write_amp};
}

void print_result(const WriteResult& r) {
    printf("  %-20s  %8.0f ops/s  %6.1f MB/s  %.1fx write-amp  %s on disk\n",
           r.strategy_name.c_str(),
           r.ops_per_sec,
           r.mb_per_sec,
           r.write_amp,
           human_bytes(r.disk_bytes).c_str());
}

int main() {
    const int    NUM_KEYS  = 200'000;
    const size_t VAL_SIZE  = 100;   // bytes

    printf("\n");
    print_header("Mini-LSM Write Throughput Benchmark");
    printf("  Keys: %d  |  Key size: ~%zu B  |  Value size: %zu B\n",
           NUM_KEYS, gen_key(0).size(), VAL_SIZE);
    printf("  Total user data: %s\n",
           human_bytes((uintmax_t)NUM_KEYS * (gen_key(0).size() + VAL_SIZE)).c_str());
    printf("\n");

    std::vector<WriteResult> results;

    // 1. NoCompaction (baseline — no compaction overhead)
    {
        auto opts = LsmStorageOptions::default_for_week1_test();
        opts.block_size = 4096;
        opts.target_sst_size = 2 << 20;
        results.push_back(run_write_bench("NoCompaction", opts, NUM_KEYS, VAL_SIZE));
    }

    // 2. SimpleLeveled
    {
        auto opts = LsmStorageOptions::default_for_week2_test(
            CompactionOptions::simple(SimpleLeveledCompactionOptions{0, 200, 4}));
        opts.block_size = 4096;
        opts.target_sst_size = 2 << 20;
        results.push_back(run_write_bench("SimpleLeveled", opts, NUM_KEYS, VAL_SIZE));
    }

    // 3. Tiered
    {
        TieredCompactionOptions tiered_opts;
        tiered_opts.num_tiers = 3;
        tiered_opts.max_size_amplification_percent = 200;
        tiered_opts.size_ratio = 1;
        tiered_opts.min_merge_width = 2;
        auto opts = LsmStorageOptions::default_for_week2_test(
            CompactionOptions::tiered(tiered_opts));
        opts.block_size = 4096;
        opts.target_sst_size = 2 << 20;
        results.push_back(run_write_bench("Tiered", opts, NUM_KEYS, VAL_SIZE));
    }

    // 4. Leveled
    {
        LeveledCompactionOptions lev_opts;
        lev_opts.level_size_multiplier = 2;
        lev_opts.level0_file_num_compaction_trigger = 2;
        lev_opts.max_levels = 4;
        lev_opts.base_level_size_mb = 16;
        auto opts = LsmStorageOptions::default_for_week2_test(
            CompactionOptions::leveled(lev_opts));
        opts.block_size = 4096;
        opts.target_sst_size = 2 << 20;
        results.push_back(run_write_bench("Leveled", opts, NUM_KEYS, VAL_SIZE));
    }

    // Print results table
    printf("  %-20s  %14s  %10s  %12s  %14s\n",
           "Strategy", "Ops/sec", "MB/sec", "Write Amp", "Disk Size");
    print_separator();
    for (const auto& r : results) print_result(r);
    printf("\n");
    printf("  * Write amplification = (bytes written to disk) / (bytes written by user)\n");
    printf("  * Lower write amp = better for write-heavy workloads\n");
    printf("\n");

    return 0;
}
