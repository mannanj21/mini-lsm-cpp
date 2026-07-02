#include "mini_lsm/mem_table.h"
#include <cassert>

namespace mini_lsm {

MemTable::MemTable(size_t id) : id_(id) {}

MemTable::MemTable(size_t id, std::shared_ptr<Wal> wal) : id_(id), wal_(std::move(wal)) {}

std::variant<std::shared_ptr<MemTable>, std::string> MemTable::create_with_wal(size_t id, const std::string& path) {
    auto wal_res = Wal::create(path);
    if (std::holds_alternative<std::string>(wal_res)) {
        return std::get<std::string>(wal_res);
    }
    auto wal = std::get<std::shared_ptr<Wal>>(wal_res);
    return std::make_shared<MemTable>(id, std::move(wal));
}

std::variant<std::shared_ptr<MemTable>, std::string> MemTable::recover_from_wal(size_t id, const std::string& path) {
    std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> records;
    auto wal_res = Wal::recover(path, records);
    if (std::holds_alternative<std::string>(wal_res)) {
        return std::get<std::string>(wal_res);
    }
    auto wal = std::get<std::shared_ptr<Wal>>(wal_res);
    auto table = std::make_shared<MemTable>(id, std::move(wal));
    for (auto& [k_vec, v_vec] : records) {
        std::string k(k_vec.begin(), k_vec.end());
        std::string v(v_vec.begin(), v_vec.end());
        table->map_[std::move(k)] = std::move(v);
        table->approximate_size_.fetch_add(k_vec.size() + v_vec.size(), std::memory_order_relaxed);
    }
    return table;
}

std::variant<std::monostate, std::string> MemTable::put(KeySlice key, KeySlice value) {
    if (wal_) {
        auto res = wal_->put(key, value);
        if (std::holds_alternative<std::string>(res)) {
            return res;
        }
    }
    std::unique_lock<std::shared_mutex> lock(mu_);
    std::string k(reinterpret_cast<const char*>(key.raw_ref()), key.len());
    std::string v(reinterpret_cast<const char*>(value.raw_ref()), value.len());
    map_[std::move(k)] = std::move(v);
    approximate_size_.fetch_add(key.len() + value.len(), std::memory_order_relaxed);
    return std::monostate{};
}

std::optional<std::string> MemTable::get(KeySlice key) const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    std::string k(reinterpret_cast<const char*>(key.raw_ref()), key.len());
    auto it = map_.find(k);
    if (it != map_.end()) {
        return it->second;
    }
    return std::nullopt;
}

MemTableIterator MemTable::scan(Bound lower, Bound upper) const {
    return MemTableIterator(shared_from_this(), std::move(lower), std::move(upper));
}

void MemTable::flush(SsTableBuilder& builder) const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    for (const auto& [k, v] : map_) {
        builder.add(KeySlice(k), KeySlice(v));
    }
}

std::variant<std::monostate, std::string> MemTable::sync_wal() const {
    if (wal_) {
        return wal_->sync();
    }
    return std::monostate{};
}

bool MemTable::is_empty() const {
    std::shared_lock<std::shared_mutex> lock(mu_);
    return map_.empty();
}

} // namespace mini_lsm
