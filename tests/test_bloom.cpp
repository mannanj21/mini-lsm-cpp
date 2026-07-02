#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "mini_lsm/table.h"

using namespace mini_lsm;

TEST(BloomTest, FalsePositiveRateUnder2Percent) {
    const size_t N = 10000;
    std::vector<uint32_t> hashes;
    hashes.reserve(N);

    std::vector<std::string> keys;
    keys.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        keys.push_back("key_inserted_" + std::to_string(i));
        hashes.push_back(farmhash_fingerprint32(KeySlice(keys.back())));
    }

    Bloom bloom = Bloom::build_from_key_hashes(hashes, 10);

    // Verify all inserted keys return true
    for (uint32_t h : hashes) {
        EXPECT_TRUE(bloom.may_contain(h));
    }

    // Check FPR on 10,000 uninserted keys
    size_t false_positives = 0;
    for (size_t i = 0; i < N; ++i) {
        std::string uninserted = "key_not_inserted_" + std::to_string(i);
        uint32_t h = farmhash_fingerprint32(KeySlice(uninserted));
        if (bloom.may_contain(h)) {
            false_positives++;
        }
    }

    double fpr = static_cast<double>(false_positives) / static_cast<double>(N);
    EXPECT_LT(fpr, 0.02); // < 2% FPR
}
