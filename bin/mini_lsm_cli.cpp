#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include "mini_lsm/lsm_storage.h"

using namespace mini_lsm;

int main() {
    std::unique_ptr<MiniLsm> db;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit") {
            if (db) {
                db->close();
            }
            break;
        } else if (cmd == "open") {
            std::string path;
            if (!(iss >> path)) {
                std::cout << "Usage: open <path>\n";
                continue;
            }
            try {
                if (db) {
                    db->close();
                }
                auto opts = LsmStorageOptions::default_for_week1_test();
                opts.enable_wal = true;
                db = MiniLsm::open(path, opts);
                std::cout << "OK\n";
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << "\n";
            }
        } else if (cmd == "put") {
            if (!db) {
                std::cout << "Error: DB not opened. Run 'open <path>' first.\n";
                continue;
            }
            std::string k, v;
            if (!(iss >> k >> v)) {
                std::cout << "Usage: put <k> <v>\n";
                continue;
            }
            try {
                db->put(KeySlice(k), KeySlice(v));
                std::cout << "OK\n";
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << "\n";
            }
        } else if (cmd == "get") {
            if (!db) {
                std::cout << "Error: DB not opened. Run 'open <path>' first.\n";
                continue;
            }
            std::string k;
            if (!(iss >> k)) {
                std::cout << "Usage: get <k>\n";
                continue;
            }
            try {
                auto val = db->get(KeySlice(k));
                if (!val.empty()) {
                    std::cout << val << "\n";
                } else {
                    std::cout << "(not found)\n";
                }
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << "\n";
            }
        } else if (cmd == "delete") {
            if (!db) {
                std::cout << "Error: DB not opened. Run 'open <path>' first.\n";
                continue;
            }
            std::string k;
            if (!(iss >> k)) {
                std::cout << "Usage: delete <k>\n";
                continue;
            }
            try {
                db->del(KeySlice(k));
                std::cout << "OK\n";
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << "\n";
            }
        } else if (cmd == "scan") {
            if (!db) {
                std::cout << "Error: DB not opened. Run 'open <path>' first.\n";
                continue;
            }
            std::string l, u;
            if (!(iss >> l >> u)) {
                std::cout << "Usage: scan <lower> <upper>\n";
                continue;
            }
            try {
                Bound lower = (l == "*") ? Bound::unbounded() : Bound::included(KeySlice(l));
                Bound upper = (u == "*") ? Bound::unbounded() : Bound::excluded(KeySlice(u));
                auto iter = db->scan(lower, upper);
                while (iter.is_valid()) {
                    std::cout << iter.key().to_string() << " " << iter.value().to_string() << "\n";
                    iter.next();
                }
                std::cout << "OK\n";
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << "\n";
            }
        } else if (cmd == "flush") {
            if (!db) {
                std::cout << "Error: DB not opened. Run 'open <path>' first.\n";
                continue;
            }
            try {
                db->force_flush();
                std::cout << "OK\n";
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << "\n";
            }
        } else if (cmd == "compact") {
            if (!db) {
                std::cout << "Error: DB not opened. Run 'open <path>' first.\n";
                continue;
            }
            try {
                db->force_full_compaction();
                std::cout << "OK\n";
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << "\n";
            }
        } else {
            std::cout << "Unknown command: " << cmd << "\n";
        }
    }

    if (db) {
        db->close();
    }
    return 0;
}
