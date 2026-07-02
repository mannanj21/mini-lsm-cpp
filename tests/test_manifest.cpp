#include <gtest/gtest.h>
#include <unistd.h>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "mini_lsm/manifest.h"

using namespace mini_lsm;

TEST(ManifestTest, WriteRecordsAndRecoverRoundTrip) {
    const std::string path = "test_manifest_roundtrip.json";
    ::unlink(path.c_str());

    {
        auto create_res = Manifest::create(path);
        ASSERT_TRUE(std::holds_alternative<std::shared_ptr<Manifest>>(create_res));
        auto manifest = std::get<std::shared_ptr<Manifest>>(create_res);

        auto r1 = ManifestRecord::flush(101);
        auto r2 = ManifestRecord::new_memtable(102);

        SimpleLeveledCompactionTask task;
        task.upper_level = std::nullopt;
        task.upper_level_sst_ids = {101};
        task.lower_level = 1;
        task.lower_level_sst_ids = {};
        task.is_lower_level_bottom_level = true;
        auto r3 = ManifestRecord::compaction(CompactionTask::simple(task), {103});

        EXPECT_TRUE(std::holds_alternative<std::monostate>(manifest->add_record(nullptr, r1)));
        EXPECT_TRUE(std::holds_alternative<std::monostate>(manifest->add_record(nullptr, r2)));
        EXPECT_TRUE(std::holds_alternative<std::monostate>(manifest->add_record(nullptr, r3)));
    }

    using RecPair = std::pair<std::shared_ptr<Manifest>, std::vector<ManifestRecord>>;
    auto rec_res = Manifest::recover(path);
    ASSERT_TRUE(std::holds_alternative<RecPair>(rec_res));

    auto& [manifest, records] = std::get<RecPair>(rec_res);
    ASSERT_EQ(records.size(), 3);

    EXPECT_EQ(records[0].type, ManifestRecord::Type::Flush);
    EXPECT_EQ(records[0].flush_sst_id, 101);

    EXPECT_EQ(records[1].type, ManifestRecord::Type::NewMemtable);
    EXPECT_EQ(records[1].new_memtable_id, 102);

    EXPECT_EQ(records[2].type, ManifestRecord::Type::Compaction);
    EXPECT_EQ(records[2].compaction_task.type, CompactionTask::Type::Simple);
    auto& task = std::get<SimpleLeveledCompactionTask>(records[2].compaction_task.task);
    EXPECT_FALSE(task.upper_level.has_value());
    EXPECT_EQ(task.upper_level_sst_ids, (std::vector<size_t>{101}));
    EXPECT_EQ(task.lower_level, 1);
    EXPECT_EQ(records[2].compaction_output_sst_ids, (std::vector<size_t>{103}));

    ::unlink(path.c_str());
}

TEST(ManifestTest, CorruptedRecordReturnsErrorNotCrash) {
    const std::string path = "test_manifest_corrupt.json";
    ::unlink(path.c_str());

    {
        auto create_res = Manifest::create(path);
        ASSERT_TRUE(std::holds_alternative<std::shared_ptr<Manifest>>(create_res));
        auto manifest = std::get<std::shared_ptr<Manifest>>(create_res);

        auto r1 = ManifestRecord::flush(50);
        EXPECT_TRUE(std::holds_alternative<std::monostate>(manifest->add_record(nullptr, r1)));
    }

    // Corrupt one byte in the payload of the record
    {
        std::fstream fs(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(fs.is_open());
        fs.seekp(10, std::ios::beg);
        char garbage = 0x7F;
        fs.write(&garbage, 1);
        fs.close();
    }

    auto rec_res = Manifest::recover(path);
    // Confirm graceful error return (string variant) not a crash per DoD
    ASSERT_TRUE(std::holds_alternative<std::string>(rec_res));
    EXPECT_NE(std::get<std::string>(rec_res).find("mismatch"), std::string::npos);

    ::unlink(path.c_str());
}
