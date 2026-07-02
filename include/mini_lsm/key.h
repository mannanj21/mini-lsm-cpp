#ifndef MINI_LSM_KEY_H
#define MINI_LSM_KEY_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace mini_lsm {

class KeyVec;

class KeySlice {
public:
    KeySlice() : ptr_(nullptr), len_(0) {}
    KeySlice(const uint8_t* ptr, size_t len) : ptr_(ptr), len_(len) {}
    KeySlice(const char* ptr, size_t len)
        : ptr_(reinterpret_cast<const uint8_t*>(ptr)), len_(len) {}
    /* implicit */ KeySlice(const char* s)
        : ptr_(reinterpret_cast<const uint8_t*>(s)), len_(s ? std::strlen(s) : 0) {}
    /* implicit */ KeySlice(const std::string& str)
        : ptr_(reinterpret_cast<const uint8_t*>(str.data())), len_(str.size()) {}
    /* implicit */ KeySlice(const std::vector<uint8_t>& vec)
        : ptr_(vec.data()), len_(vec.size()) {}
    /* implicit */ KeySlice(std::string_view sv)
        : ptr_(reinterpret_cast<const uint8_t*>(sv.data())), len_(sv.size()) {}

    static KeySlice from_slice(const uint8_t* ptr, size_t len) {
        return KeySlice(ptr, len);
    }
    static KeySlice from_slice(const std::vector<uint8_t>& vec) {
        return KeySlice(vec);
    }

    const uint8_t* raw_ref() const { return ptr_; }
    size_t len() const { return len_; }
    bool is_empty() const { return len_ == 0; }
    std::string to_string() const { return std::string(reinterpret_cast<const char*>(ptr_), len_); }

    KeyVec to_key_vec() const;

    int compare(const KeySlice& other) const {
        size_t min_len = std::min(len_, other.len_);
        if (min_len > 0) {
            int cmp = std::memcmp(ptr_, other.ptr_, min_len);
            if (cmp != 0) return cmp;
        }
        if (len_ < other.len_) return -1;
        if (len_ > other.len_) return 1;
        return 0;
    }

    bool operator==(const KeySlice& rhs) const { return compare(rhs) == 0; }
    bool operator!=(const KeySlice& rhs) const { return compare(rhs) != 0; }
    bool operator<(const KeySlice& rhs) const { return compare(rhs) < 0; }
    bool operator>(const KeySlice& rhs) const { return compare(rhs) > 0; }
    bool operator<=(const KeySlice& rhs) const { return compare(rhs) <= 0; }
    bool operator>=(const KeySlice& rhs) const { return compare(rhs) >= 0; }

private:
    const uint8_t* ptr_;
    size_t len_;
};

class KeyVec {
public:
    KeyVec() = default;
    explicit KeyVec(std::vector<uint8_t> data) : data_(std::move(data)) {}
    static KeyVec from_vec(std::vector<uint8_t> data) {
        return KeyVec(std::move(data));
    }
    static KeyVec from_slice(const uint8_t* ptr, size_t len) {
        return KeyVec(std::vector<uint8_t>(ptr, ptr + len));
    }

    void clear() { data_.clear(); }

    void append(const uint8_t* ptr, size_t len) {
        data_.insert(data_.end(), ptr, ptr + len);
    }
    void append(KeySlice slice) {
        append(slice.raw_ref(), slice.len());
    }

    void set_from_slice(KeySlice slice) {
        data_.clear();
        append(slice);
    }

    KeySlice as_key_slice() const {
        return KeySlice(data_.data(), data_.size());
    }
    KeySlice as_slice() const { return as_key_slice(); }

    const uint8_t* raw_ref() const { return data_.data(); }
    const std::vector<uint8_t>& into_inner() const { return data_; }
    std::vector<uint8_t>& into_inner() { return data_; }

    size_t len() const { return data_.size(); }
    bool is_empty() const { return data_.empty(); }

    bool operator==(const KeySlice& rhs) const { return as_key_slice() == rhs; }
    bool operator!=(const KeySlice& rhs) const { return as_key_slice() != rhs; }
    bool operator<(const KeySlice& rhs) const { return as_key_slice() < rhs; }
    bool operator>(const KeySlice& rhs) const { return as_key_slice() > rhs; }
    bool operator<=(const KeySlice& rhs) const { return as_key_slice() <= rhs; }
    bool operator>=(const KeySlice& rhs) const { return as_key_slice() >= rhs; }

    bool operator==(const KeyVec& rhs) const { return as_key_slice() == rhs.as_key_slice(); }
    bool operator!=(const KeyVec& rhs) const { return as_key_slice() != rhs.as_key_slice(); }
    bool operator<(const KeyVec& rhs) const { return as_key_slice() < rhs.as_key_slice(); }
    bool operator>(const KeyVec& rhs) const { return as_key_slice() > rhs.as_key_slice(); }
    bool operator<=(const KeyVec& rhs) const { return as_key_slice() <= rhs.as_key_slice(); }
    bool operator>=(const KeyVec& rhs) const { return as_key_slice() >= rhs.as_key_slice(); }

private:
    std::vector<uint8_t> data_;
};

inline KeyVec KeySlice::to_key_vec() const {
    return KeyVec(std::vector<uint8_t>(ptr_, ptr_ + len_));
}

inline bool operator==(const KeySlice& lhs, const KeyVec& rhs) { return lhs == rhs.as_key_slice(); }
inline bool operator!=(const KeySlice& lhs, const KeyVec& rhs) { return lhs != rhs.as_key_slice(); }
inline bool operator<(const KeySlice& lhs, const KeyVec& rhs) { return lhs < rhs.as_key_slice(); }
inline bool operator>(const KeySlice& lhs, const KeyVec& rhs) { return lhs > rhs.as_key_slice(); }
inline bool operator<=(const KeySlice& lhs, const KeyVec& rhs) { return lhs <= rhs.as_key_slice(); }
inline bool operator>=(const KeySlice& lhs, const KeyVec& rhs) { return lhs >= rhs.as_key_slice(); }

} // namespace mini_lsm

#endif // MINI_LSM_KEY_H
