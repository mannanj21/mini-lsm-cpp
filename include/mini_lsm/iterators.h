#ifndef MINI_LSM_ITERATORS_H
#define MINI_LSM_ITERATORS_H

#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "mini_lsm/key.h"
#include "mini_lsm/table.h"

namespace mini_lsm {

enum class BoundType {
    Unbounded,
    Included,
    Excluded
};

struct Bound {
    BoundType type{BoundType::Unbounded};
    KeySlice key{};

    static Bound unbounded() { return Bound{BoundType::Unbounded, KeySlice()}; }
    static Bound included(KeySlice k) { return Bound{BoundType::Included, k}; }
    static Bound excluded(KeySlice k) { return Bound{BoundType::Excluded, k}; }
};

class StorageIterator {
public:
    virtual ~StorageIterator() = default;
    virtual KeySlice key() const = 0;
    virtual KeySlice value() const = 0;
    virtual bool is_valid() const = 0;
    virtual void next() = 0;
};

template <typename T>
class StorageIteratorWrapper : public StorageIterator {
public:
    StorageIteratorWrapper() = default;
    explicit StorageIteratorWrapper(T iter) : iter_(std::move(iter)) {}
    KeySlice key() const override { return iter_.key(); }
    KeySlice value() const override { return iter_.value(); }
    bool is_valid() const override { return iter_.is_valid(); }
    void next() override { iter_.next(); }
    T& inner() { return iter_; }
    const T& inner() const { return iter_; }
private:
    T iter_;
};

template <typename A, typename B>
class TwoMergeIterator : public StorageIterator {
public:
    TwoMergeIterator() = default;
    TwoMergeIterator(A a, B b) : a_(std::move(a)), b_(std::move(b)), first_(false) {
        skip_b();
        first_ = choose_a();
    }

    static TwoMergeIterator create(A a, B b) {
        return TwoMergeIterator(std::move(a), std::move(b));
    }

    KeySlice key() const override {
        return first_ ? a_.key() : b_.key();
    }

    KeySlice value() const override {
        return first_ ? a_.value() : b_.value();
    }

    bool is_valid() const override {
        return first_ ? a_.is_valid() : b_.is_valid();
    }

    void next() override {
        if (first_) {
            a_.next();
        } else {
            b_.next();
        }
        skip_b();
        first_ = choose_a();
    }

private:
    void skip_b() {
        if (a_.is_valid()) {
            if (b_.is_valid() && b_.key() == a_.key()) {
                b_.next();
            }
        }
    }

    bool choose_a() const {
        if (!a_.is_valid()) return false;
        if (!b_.is_valid()) return true;
        return a_.key() < b_.key();
    }

    A a_;
    B b_;
    bool first_{false};
};

template <typename I>
class MergeIterator : public StorageIterator {
public:
    struct HeapWrapper {
        size_t index;
        I iter;

        bool operator<(const HeapWrapper& other) const {
            int cmp = iter.key().compare(other.iter.key());
            if (cmp != 0) {
                return cmp > 0;
            }
            return index > other.index;
        }
    };

    MergeIterator() = default;
    explicit MergeIterator(std::vector<I> iters) {
        for (size_t i = 0; i < iters.size(); ++i) {
            if (iters[i].is_valid()) {
                heap_.push_back(HeapWrapper{i, std::move(iters[i])});
            }
        }
        std::make_heap(heap_.begin(), heap_.end());
        if (!heap_.empty()) {
            std::pop_heap(heap_.begin(), heap_.end());
            current_ = std::move(heap_.back());
            heap_.pop_back();
        }
    }

    static MergeIterator create(std::vector<I> iters) {
        return MergeIterator(std::move(iters));
    }

    KeySlice key() const override {
        return current_->iter.key();
    }

    KeySlice value() const override {
        return current_->iter.value();
    }

    bool is_valid() const override {
        return current_.has_value() && current_->iter.is_valid();
    }

    void next() override {
        assert(current_.has_value());
        while (!heap_.empty() && heap_.front().iter.key() == current_->iter.key()) {
            std::pop_heap(heap_.begin(), heap_.end());
            HeapWrapper top = std::move(heap_.back());
            heap_.pop_back();

            top.iter.next();
            if (top.iter.is_valid()) {
                heap_.push_back(std::move(top));
                std::push_heap(heap_.begin(), heap_.end());
            }
        }

        current_->iter.next();
        if (!current_->iter.is_valid()) {
            if (!heap_.empty()) {
                std::pop_heap(heap_.begin(), heap_.end());
                current_ = std::move(heap_.back());
                heap_.pop_back();
            } else {
                current_.reset();
            }
            return;
        }

        if (!heap_.empty()) {
            if (*current_ < heap_.front()) {
                std::pop_heap(heap_.begin(), heap_.end());
                HeapWrapper top = std::move(heap_.back());
                heap_.pop_back();

                std::swap(top, *current_);
                heap_.push_back(std::move(top));
                std::push_heap(heap_.begin(), heap_.end());
            }
        }
    }

private:
    std::vector<HeapWrapper> heap_;
    std::optional<HeapWrapper> current_;
};

class SstConcatIterator : public StorageIterator {
public:
    SstConcatIterator() = default;
    static SstConcatIterator create_and_seek_to_first(std::vector<std::shared_ptr<SsTable>> sstables);
    static SstConcatIterator create_and_seek_to_key(std::vector<std::shared_ptr<SsTable>> sstables, KeySlice key);

    KeySlice key() const override;
    KeySlice value() const override;
    bool is_valid() const override;
    void next() override;

private:
    void check_sst_valid();
    void move_until_valid();

    std::optional<SsTableIterator> current_;
    size_t next_sst_idx_{0};
    std::vector<std::shared_ptr<SsTable>> sstables_;
};

} // namespace mini_lsm

#endif // MINI_LSM_ITERATORS_H
