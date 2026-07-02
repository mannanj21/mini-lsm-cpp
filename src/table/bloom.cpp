#include "mini_lsm/table.h"
#include "mini_lsm/endian.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include "farmhash.h"

namespace mini_lsm {

uint32_t farmhash_fingerprint32(KeySlice key) {
    return util::Fingerprint32(reinterpret_cast<const char*>(key.raw_ref()), key.len());
}

size_t Bloom::bloom_bits_per_key(size_t entries, double false_positive_rate) {
    double size = -(static_cast<double>(entries) * std::log(false_positive_rate)) / (0.6931471805599453 * 0.6931471805599453);
    double locs = std::ceil(size / static_cast<double>(entries));
    return static_cast<size_t>(locs);
}

Bloom Bloom::build_from_key_hashes(const std::vector<uint32_t>& hashes, size_t bits_per_key) {
    uint32_t k = static_cast<uint32_t>(static_cast<double>(bits_per_key) * 0.69);
    k = std::min<uint32_t>(std::max<uint32_t>(k, 1), 30);
    size_t nbits = std::max<size_t>(hashes.size() * bits_per_key, 64);
    size_t nbytes = (nbits + 7) / 8;
    nbits = nbytes * 8;

    std::vector<uint8_t> filter(nbytes, 0);
    for (uint32_t h : hashes) {
        uint32_t delta = (h << 15) | (h >> 17);
        for (uint32_t i = 0; i < k; ++i) {
            size_t bit_pos = h % nbits;
            filter[bit_pos / 8] |= (1 << (bit_pos % 8));
            h += delta;
        }
    }
    return Bloom{std::move(filter), static_cast<uint8_t>(k)};
}

bool Bloom::may_contain(uint32_t h) const {
    if (k > 30) {
        return true;
    }
    size_t nbits = filter.size() * 8;
    if (nbits == 0) {
        return true;
    }
    uint32_t delta = (h << 15) | (h >> 17);
    for (uint32_t i = 0; i < k; ++i) {
        size_t bit_pos = h % nbits;
        if (!(filter[bit_pos / 8] & (1 << (bit_pos % 8)))) {
            return false;
        }
        h += delta;
    }
    return true;
}

void Bloom::encode(std::vector<uint8_t>& buf) const {
    size_t offset = buf.size();
    buf.insert(buf.end(), filter.begin(), filter.end());
    buf.push_back(k);
    uint32_t checksum = crc32_hash(buf.data() + offset, buf.size() - offset);
    put_u32_be(buf, checksum);
}

Bloom Bloom::decode(const uint8_t* raw, size_t len) {
    assert(len >= 5);
    uint32_t expected_checksum = read_u32_be(raw + len - 4);
    uint32_t actual_checksum = crc32_hash(raw, len - 4);
    assert(expected_checksum == actual_checksum);
    uint8_t k = raw[len - 5];
    std::vector<uint8_t> filter(raw, raw + len - 5);
    return Bloom{std::move(filter), k};
}

} // namespace mini_lsm
