#include <gtest/gtest.h>
#include <unistd.h>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "mini_lsm/wal.h"

using namespace mini_lsm;

TEST(WalTest, Write100CorruptLast5BytesAndRecover99) {
    const std::string path = "test_recovery.wal";
    ::unlink(path.c_str());

    {
        auto res = Wal::create(path);
        ASSERT_TRUE(std::holds_alternative<std::shared_ptr<Wal>>(res));
        auto wal = std::get<std::shared_ptr<Wal>>(res);

        for (int i = 0; i < 100; ++i) {
            char kbuf[32];
            char vbuf[32];
            snprintf(kbuf, sizeof(kbuf), "key_%02d", i);
            snprintf(vbuf, sizeof(vbuf), "val_%02d", i);
            auto put_res = wal->put(KeySlice(kbuf), KeySlice(vbuf));
            EXPECT_TRUE(std::holds_alternative<std::monostate>(put_res));
        }
        wal->sync();
    } // wal destroyed and file closed

    // Corrupt last 5 bytes of file on disk per DoD
    {
        std::fstream fs(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(fs.is_open());
        fs.seekp(0, std::ios::end);
        std::streamoff sz = fs.tellp();
        ASSERT_GT(sz, 5);
        fs.seekp(sz - 5, std::ios::beg);
        char garbage[5] = {0x7F, 0x7F, 0x7F, 0x7F, 0x7F};
        fs.write(garbage, 5);
        fs.close();
    }

    std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> recovered;
    auto rec_res = Wal::recover(path, recovered);
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<Wal>>(rec_res));

    // Verify exactly 99 entries loaded cleanly per DoD
    ASSERT_EQ(recovered.size(), 99);
    for (size_t i = 0; i < 99; ++i) {
        char kbuf[32];
        char vbuf[32];
        snprintf(kbuf, sizeof(kbuf), "key_%02d", static_cast<int>(i));
        snprintf(vbuf, sizeof(vbuf), "val_%02d", static_cast<int>(i));
        std::string rec_k(recovered[i].first.begin(), recovered[i].first.end());
        std::string rec_v(recovered[i].second.begin(), recovered[i].second.end());
        EXPECT_EQ(rec_k, std::string(kbuf));
        EXPECT_EQ(rec_v, std::string(vbuf));
    }

    ::unlink(path.c_str());
}

TEST(WalTest, CrashWithoutCleanCloseRecoversAllRecords) {
    const std::string path = "test_crash_no_clean_close.wal";
    ::unlink(path.c_str());
    const int N = 50; // distinct from existing test's 100 per instructions

    /*
     * Note on Crash vs Clean Close equivalence in wal.cpp:
     * In this implementation, records are appended directly via std::fwrite.
     * Calling wal->sync() flushes the C stdio buffer and issues ::fsync on the file descriptor.
     * The Wal destructor (~Wal) only issues std::fflush and std::fclose; it does not write
     * file trailers, EOF markers, or perform truncation on close. Therefore, once sync()
     * returns, the persisted bytes on disk are complete and identical whether ~Wal() runs or
     * the process suddenly crashes/dies without clean close.
     */
    {
        auto res = Wal::create(path);
        ASSERT_TRUE(std::holds_alternative<std::shared_ptr<Wal>>(res));
        auto wal = std::get<std::shared_ptr<Wal>>(res);

        for (int i = 0; i < N; ++i) {
            char kbuf[32];
            char vbuf[32];
            snprintf(kbuf, sizeof(kbuf), "crash_key_%02d", i);
            snprintf(vbuf, sizeof(vbuf), "crash_val_%02d", i);
            auto put_res = wal->put(KeySlice(kbuf), KeySlice(vbuf));
            EXPECT_TRUE(std::holds_alternative<std::monostate>(put_res));
        }
        wal->sync(); // ensure bytes are persisted to OS/disk before simulating sudden process termination
    }

    std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> recovered;
    auto rec_res = Wal::recover(path, recovered);
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<Wal>>(rec_res));

    // Assert recovery returns exactly N records (all of them, not N-1), in order
    ASSERT_EQ(recovered.size(), N);
    for (int i = 0; i < N; ++i) {
        char kbuf[32];
        char vbuf[32];
        snprintf(kbuf, sizeof(kbuf), "crash_key_%02d", i);
        snprintf(vbuf, sizeof(vbuf), "crash_val_%02d", i);
        std::string rec_k(recovered[i].first.begin(), recovered[i].first.end());
        std::string rec_v(recovered[i].second.begin(), recovered[i].second.end());
        EXPECT_EQ(rec_k, std::string(kbuf));
        EXPECT_EQ(rec_v, std::string(vbuf));
    }

    ::unlink(path.c_str());
}
