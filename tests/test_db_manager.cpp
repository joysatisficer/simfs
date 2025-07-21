#include <gtest/gtest.h>
#include "db_manager.h"
#include <filesystem>
#include <string>
#include <algorithm>

class DBManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "./test_db_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed());
        db_ = std::make_unique<DBManager>(test_db_path_);
    }

    void TearDown() override {
        db_.reset();
        std::filesystem::remove_all(test_db_path_);
    }

    std::string test_db_path_;
    std::unique_ptr<DBManager> db_;
};

TEST_F(DBManagerTest, PutAndGet) {
    const std::string key = "test_key";
    const std::string value = "test_value";
    
    EXPECT_TRUE(db_->put(key, value));
    
    std::string retrieved_value;
    EXPECT_TRUE(db_->get(key, retrieved_value));
    EXPECT_EQ(value, retrieved_value);
}

TEST_F(DBManagerTest, GetNonExistentKey) {
    std::string value;
    EXPECT_FALSE(db_->get("non_existent_key", value));
}

TEST_F(DBManagerTest, Exists) {
    const std::string key = "existing_key";
    const std::string value = "some_value";
    
    EXPECT_FALSE(db_->exists(key));
    
    db_->put(key, value);
    EXPECT_TRUE(db_->exists(key));
}

TEST_F(DBManagerTest, Remove) {
    const std::string key = "key_to_remove";
    const std::string value = "value_to_remove";
    
    db_->put(key, value);
    EXPECT_TRUE(db_->exists(key));
    
    EXPECT_TRUE(db_->remove(key));
    EXPECT_FALSE(db_->exists(key));
}

TEST_F(DBManagerTest, ListKeys) {
    db_->put("prefix/key1", "value1");
    db_->put("prefix/key2", "value2");
    db_->put("prefix/key3", "value3");
    db_->put("other/key", "value");
    
    auto keys = db_->listKeys("prefix/");
    EXPECT_EQ(keys.size(), 3);
    
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "prefix/key1") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "prefix/key2") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "prefix/key3") != keys.end());
}

TEST_F(DBManagerTest, EmptyPrefix) {
    db_->put("key1", "value1");
    db_->put("key2", "value2");
    
    auto keys = db_->listKeys("");
    EXPECT_GE(keys.size(), 2);
}