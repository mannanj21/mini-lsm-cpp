#ifndef MINI_LSM_WAL_H
#define MINI_LSM_WAL_H

#include <cstdio>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include "mini_lsm/key.h"

namespace mini_lsm {

class Wal {
public:
    ~Wal();
    static std::variant<std::shared_ptr<Wal>, std::string> create(const std::string& path);
    static std::variant<std::shared_ptr<Wal>, std::string> recover(
        const std::string& path,
        std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& recovered_records);

    std::variant<std::monostate, std::string> put(KeySlice key, KeySlice value);
    std::variant<std::monostate, std::string> put_batch(const std::vector<std::pair<KeySlice, KeySlice>>& batch);
    std::variant<std::monostate, std::string> sync();

private:
    Wal(const std::string& path, FILE* fp);
    std::string path_;
    FILE* fp_{nullptr};
    std::mutex mu_;
};

} // namespace mini_lsm

#endif // MINI_LSM_WAL_H
