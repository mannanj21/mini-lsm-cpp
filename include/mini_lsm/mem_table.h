#ifndef MINI_LSM_MEM_TABLE_H
#define MINI_LSM_MEM_TABLE_H

#include <atomic>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <variant>
#include <vector>
#include "mini_lsm/iterators.h"
#include "mini_lsm/key.h"
#include "mini_lsm/table.h"
#include "mini_lsm/wal.h"

namespace mini_lsm {

class MemTableIterator;

class MemTable : public std::enable_shared_from_this<MemTable> {
public:
    explicit MemTable(size_t id);
    MemTable(size_t id, std::shared_ptr<Wal> wal);

    static std::variant<std::shared_ptr<MemTable>, std::string> create_with_wal(size_t id, const std::string& path);
    static std::variant<std::shared_ptr<MemTable>, std::string> recover_from_wal(size_t id, const std::string& path);

    std::variant<std::monostate, std::string> put(KeySlice key, KeySlice value);
    std::optional<std::string> get(KeySlice key) const;

    MemTableIterator scan(Bound lower, Bound upper) const;

    void flush(SsTableBuilder& builder) const;
    std::variant<std::monostate, std::string> sync_wal() const;

    size_t id() const { return id_; }
    size_t approximate_size() const { return approximate_size_.load(std::memory_order_relaxed); }
    bool is_empty() const;

    friend class MemTableIterator;

private:
    size_t id_;
    mutable std::shared_mutex mu_;
    std::map<std::string, std::string> map_;
    std::shared_ptr<Wal> wal_;
    std::atomic<size_t> approximate_size_{0};
};

class MemTableIterator : public StorageIterator {
public:
    MemTableIterator() = default;
    explicit MemTableIterator(std::shared_ptr<const MemTable> table);
    MemTableIterator(std::shared_ptr<const MemTable> table, Bound lower, Bound upper);

    KeySlice key() const override;
    KeySlice value() const override;
    bool is_valid() const override;
    void next() override;

private:
    std::shared_ptr<const MemTable> table_;
    std::vector<std::pair<std::string, std::string>> snapshot_;
    size_t idx_{0};
};

} // namespace mini_lsm

#endif // MINI_LSM_MEM_TABLE_H
