#ifndef MINI_LSM_LSM_ITERATOR_H
#define MINI_LSM_LSM_ITERATOR_H

#include <cassert>
#include <memory>
#include <utility>
#include "mini_lsm/iterators.h"

namespace mini_lsm {

class LsmIterator : public StorageIterator {
public:
    LsmIterator() = default;
    LsmIterator(std::unique_ptr<StorageIterator> inner, Bound end_bound);

    template <typename T>
    static LsmIterator create(T iter, Bound end_bound) {
        return LsmIterator(std::make_unique<StorageIteratorWrapper<T>>(std::move(iter)), std::move(end_bound));
    }

    static LsmIterator create(std::unique_ptr<StorageIterator> inner, Bound end_bound) {
        return LsmIterator(std::move(inner), std::move(end_bound));
    }

    KeySlice key() const override;
    KeySlice value() const override;
    bool is_valid() const override;
    void next() override;

private:
    void next_inner();
    void match_end_bound();
    void move_to_non_delete();

    std::unique_ptr<StorageIterator> inner_;
    Bound end_bound_;
    bool is_valid_{false};
};

class FusedIterator : public StorageIterator {
public:
    FusedIterator() = default;
    explicit FusedIterator(std::unique_ptr<StorageIterator> iter)
        : iter_(std::move(iter)), has_errored_(false) {}

    template <typename T>
    static FusedIterator create(T iter) {
        return FusedIterator(std::make_unique<StorageIteratorWrapper<T>>(std::move(iter)));
    }

    static FusedIterator create(std::unique_ptr<StorageIterator> iter) {
        return FusedIterator(std::move(iter));
    }

    KeySlice key() const override {
        assert(is_valid());
        return iter_->key();
    }

    KeySlice value() const override {
        assert(is_valid());
        return iter_->value();
    }

    bool is_valid() const override {
        return !has_errored_ && iter_ && iter_->is_valid();
    }

    void next() override {
        if (has_errored_ || !iter_ || !iter_->is_valid()) {
            return;
        }
        try {
            iter_->next();
        } catch (...) {
            has_errored_ = true;
            throw;
        }
    }

private:
    std::unique_ptr<StorageIterator> iter_;
    bool has_errored_{false};
};

} // namespace mini_lsm

#endif // MINI_LSM_LSM_ITERATOR_H
