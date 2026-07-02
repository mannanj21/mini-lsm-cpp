#ifndef MINI_LSM_ENDIAN_H
#define MINI_LSM_ENDIAN_H

#include <cstdint>
#include <vector>
#include <zlib.h>

namespace mini_lsm {

inline uint32_t crc32_hash(const uint8_t* data, size_t len) {
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(len));
    return static_cast<uint32_t>(crc);
}

inline void put_u16_be(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

inline void put_u32_be(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

inline void put_u64_be(std::vector<uint8_t>& buf, uint64_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 56) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 48) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 40) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 32) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

inline void write_u16_be(uint8_t* dst, uint16_t val) {
    dst[0] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dst[1] = static_cast<uint8_t>(val & 0xFF);
}

inline void write_u32_be(uint8_t* dst, uint32_t val) {
    dst[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dst[3] = static_cast<uint8_t>(val & 0xFF);
}

inline void write_u64_be(uint8_t* dst, uint64_t val) {
    dst[0] = static_cast<uint8_t>((val >> 56) & 0xFF);
    dst[1] = static_cast<uint8_t>((val >> 48) & 0xFF);
    dst[2] = static_cast<uint8_t>((val >> 40) & 0xFF);
    dst[3] = static_cast<uint8_t>((val >> 32) & 0xFF);
    dst[4] = static_cast<uint8_t>((val >> 24) & 0xFF);
    dst[5] = static_cast<uint8_t>((val >> 16) & 0xFF);
    dst[6] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dst[7] = static_cast<uint8_t>(val & 0xFF);
}

inline uint16_t read_u16_be(const uint8_t* src) {
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(src[0]) << 8) |
         static_cast<uint16_t>(src[1])
    );
}

inline uint32_t read_u32_be(const uint8_t* src) {
    return static_cast<uint32_t>(
        (static_cast<uint32_t>(src[0]) << 24) |
        (static_cast<uint32_t>(src[1]) << 16) |
        (static_cast<uint32_t>(src[2]) << 8)  |
         static_cast<uint32_t>(src[3])
    );
}

inline uint64_t read_u64_be(const uint8_t* src) {
    return static_cast<uint64_t>(
        (static_cast<uint64_t>(src[0]) << 56) |
        (static_cast<uint64_t>(src[1]) << 48) |
        (static_cast<uint64_t>(src[2]) << 40) |
        (static_cast<uint64_t>(src[3]) << 32) |
        (static_cast<uint64_t>(src[4]) << 24) |
        (static_cast<uint64_t>(src[5]) << 16) |
        (static_cast<uint64_t>(src[6]) << 8)  |
         static_cast<uint64_t>(src[7])
    );
}

} // namespace mini_lsm

#endif // MINI_LSM_ENDIAN_H
