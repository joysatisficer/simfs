#include "db_manager.h"
#include <rocksdb/options.h>
#include <iostream>

DBManager::DBManager(const std::string& db_path) {
    rocksdb::Options options;
    options.create_if_missing = true;
    
    rocksdb::DB* db_ptr;
    rocksdb::Status status = rocksdb::DB::Open(options, db_path, &db_ptr);
    
    if (!status.ok()) {
        throw std::runtime_error("Failed to open database: " + status.ToString());
    }
    
    db_.reset(db_ptr);
}

DBManager::~DBManager() = default;

bool DBManager::put(const std::string& key, const std::string& value) {
    rocksdb::Status status = db_->Put(rocksdb::WriteOptions(), key, value);
    return status.ok();
}

bool DBManager::get(const std::string& key, std::string& value) {
    rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), key, &value);
    return status.ok();
}

bool DBManager::remove(const std::string& key) {
    rocksdb::Status status = db_->Delete(rocksdb::WriteOptions(), key);
    return status.ok();
}

bool DBManager::exists(const std::string& key) {
    std::string value;
    rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), key, &value);
    return status.ok();
}

std::vector<std::string> DBManager::listKeys(const std::string& prefix) {
    std::vector<std::string> keys;
    
    rocksdb::Iterator* it = db_->NewIterator(rocksdb::ReadOptions());
    for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix); it->Next()) {
        keys.push_back(it->key().ToString());
    }
    
    delete it;
    return keys;
}