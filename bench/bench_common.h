#pragma once

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cassert>

#include "mini_lsm/lsm_storage.h"

namespace mini_lsm::bench {

// ── Timer ──────────────────────────────────────────────────────────────────

class Timer {
public:
    Timer() : start_(std::chrono::steady_clock::now()) {}

    void reset() { start_ = std::chrono::steady_clock::now(); }

    double elapsed_ms() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(now - start_).count();
    }

    double elapsed_us() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(now - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

// ── Latency histogram ──────────────────────────────────────────────────────

class LatencyHistogram {
public:
    void record(double us) { samples_.push_back(us); }

    void sort() { std::sort(samples_.begin(), samples_.end()); }

    double percentile(double p) const {
        if (samples_.empty()) return 0.0;
        size_t idx = static_cast<size_t>(p / 100.0 * samples_.size());
        if (idx >= samples_.size()) idx = samples_.size() - 1;
        return samples_[idx];
    }

    double p50() { sort(); return percentile(50); }
    double p99() { sort(); return percentile(99); }
    double p999() { sort(); return percentile(99.9); }
    size_t count() const { return samples_.size(); }

private:
    std::vector<double> samples_;
};

// ── Key/value generation ───────────────────────────────────────────────────

// Generates a fixed-length key: "key_00000042" (16 bytes with padding)
inline std::string gen_key(int i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%011d", i);
    return {buf};
}

// Generates a value of approximately val_size bytes
inline std::string gen_val(int i, size_t val_size = 100) {
    // Start with a recognizable prefix, then pad to val_size
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "val_%08d_", i);
    std::string v(prefix);
    while (v.size() < val_size) v += 'x';
    v.resize(val_size);
    return v;
}

// ── Temp-directory DB helper ───────────────────────────────────────────────

class TempDB {
public:
    explicit TempDB(const std::string& label, LsmStorageOptions opts) {
        dir_ = "/tmp/bench_" + label + "_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(dir_);
        db_ = MiniLsm::open(dir_, opts);
    }

    ~TempDB() {
        if (db_) db_->close();
        std::filesystem::remove_all(dir_);
    }

    MiniLsm* db() { return db_.get(); }

    // Returns total bytes of all .sst files on disk
    uintmax_t sst_bytes() const {
        uintmax_t total = 0;
        for (auto& e : std::filesystem::directory_iterator(dir_)) {
            if (e.path().extension() == ".sst")
                total += e.file_size();
        }
        return total;
    }

    const std::string& dir() const { return dir_; }

private:
    std::string dir_;
    std::unique_ptr<MiniLsm> db_;
};

// ── Formatting helpers ─────────────────────────────────────────────────────

inline void print_separator(char c = '-', int width = 70) {
    for (int i = 0; i < width; ++i) putchar(c);
    putchar('\n');
}

inline void print_header(const char* title) {
    print_separator('=');
    printf("  %s\n", title);
    print_separator('=');
}

inline std::string human_bytes(uintmax_t bytes) {
    if (bytes > 1024 * 1024 * 1024)
        return std::to_string(bytes / (1024 * 1024 * 1024)) + " GB";
    if (bytes > 1024 * 1024)
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    if (bytes > 1024)
        return std::to_string(bytes / 1024) + " KB";
    return std::to_string(bytes) + " B";
}

} // namespace mini_lsm::bench
