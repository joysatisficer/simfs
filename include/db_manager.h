#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <string>
#include <memory>
#include <vector>
#include <rocksdb/db.h>

class DBManager {
public:
    DBManager(const std::string& db_path);
    ~DBManager();

    bool put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    bool remove(const std::string& key);
    bool exists(const std::string& key);
    std::vector<std::string> listKeys(const std::string& prefix);

private:
    std::unique_ptr<rocksdb::DB> db_;
};

#endif