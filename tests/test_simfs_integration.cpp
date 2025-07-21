#include <gtest/gtest.h>
#include "simfs.h"
#include "db_manager.h"
#include <filesystem>
#include <thread>
#include <chrono>
#include <fstream>
#include <sys/mount.h>
#include <algorithm>

class SimFSIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "./test_simfs_db_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed());
        test_mount_path_ = "./test_mount_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed());
        
        std::filesystem::create_directory(test_mount_path_);
        
        const char* endpoint = std::getenv("LLM_ENDPOINT");
        if (!endpoint) {
            endpoint = "https://api.openai.com/v1/chat/completions";
        }
        
        simfs_ = std::make_unique<SimFS>(test_db_path_, endpoint);
        SimFS::setInstance(simfs_.get());
    }

    void TearDown() override {
        simfs_.reset();
        std::filesystem::remove_all(test_db_path_);
        std::filesystem::remove_all(test_mount_path_);
    }

    std::string test_db_path_;
    std::string test_mount_path_;
    std::unique_ptr<SimFS> simfs_;
};

TEST_F(SimFSIntegrationTest, BasicFileOperations) {
    struct stat stbuf;
    
    EXPECT_EQ(0, SimFS::getattr("/", &stbuf, nullptr));
    EXPECT_TRUE(S_ISDIR(stbuf.st_mode));
    
    struct fuse_file_info fi = {0};
    fi.flags = O_CREAT | O_RDWR;
    EXPECT_EQ(0, SimFS::create("/test.txt", 0644, &fi));
    
    EXPECT_EQ(0, SimFS::getattr("/test.txt", &stbuf, nullptr));
    EXPECT_TRUE(S_ISREG(stbuf.st_mode));
    
    const char* test_content = "Hello, SimFS!";
    size_t content_len = strlen(test_content);
    EXPECT_EQ(content_len, SimFS::write("/test.txt", test_content, content_len, 0, &fi));
    
    char read_buffer[256] = {0};
    EXPECT_EQ(content_len, SimFS::read("/test.txt", read_buffer, sizeof(read_buffer), 0, &fi));
    EXPECT_STREQ(test_content, read_buffer);
    
    EXPECT_EQ(0, SimFS::unlink("/test.txt"));
    EXPECT_EQ(-ENOENT, SimFS::getattr("/test.txt", &stbuf, nullptr));
}

TEST_F(SimFSIntegrationTest, DirectoryOperations) {
    struct stat stbuf;
    
    EXPECT_EQ(0, SimFS::mkdir("/testdir", 0755));
    EXPECT_EQ(0, SimFS::getattr("/testdir", &stbuf, nullptr));
    EXPECT_TRUE(S_ISDIR(stbuf.st_mode));
    
    struct fuse_file_info fi = {0};
    fi.flags = O_CREAT | O_RDWR;
    EXPECT_EQ(0, SimFS::create("/testdir/file1.txt", 0644, &fi));
    EXPECT_EQ(0, SimFS::create("/testdir/file2.txt", 0644, &fi));
    
    class DirBuffer {
    public:
        std::vector<std::string> entries;
        
        static int filler(void *buf, const char *name, const struct stat *stbuf,
                         off_t off, enum fuse_fill_dir_flags flags) {
            (void)stbuf;
            (void)off;
            (void)flags;
            DirBuffer* buffer = static_cast<DirBuffer*>(buf);
            buffer->entries.push_back(name);
            return 0;
        }
    };
    
    DirBuffer buffer;
    EXPECT_EQ(0, SimFS::readdir("/testdir", &buffer, DirBuffer::filler, 0, nullptr, FUSE_READDIR_PLUS));
    
    EXPECT_TRUE(std::find(buffer.entries.begin(), buffer.entries.end(), ".") != buffer.entries.end());
    EXPECT_TRUE(std::find(buffer.entries.begin(), buffer.entries.end(), "..") != buffer.entries.end());
    EXPECT_TRUE(std::find(buffer.entries.begin(), buffer.entries.end(), "file1.txt") != buffer.entries.end());
    EXPECT_TRUE(std::find(buffer.entries.begin(), buffer.entries.end(), "file2.txt") != buffer.entries.end());
}

TEST_F(SimFSIntegrationTest, LLMGeneratedContent) {
    const char* api_key = std::getenv("OPENAI_API_KEY");
    if (!api_key) {
        GTEST_SKIP() << "OPENAI_API_KEY not set, skipping LLM test";
    }

    struct fuse_file_info fi = {0};
    fi.flags = O_RDONLY;
    
    EXPECT_EQ(0, SimFS::open("/virtual_readme.md", &fi));
    
    char buffer[4096] = {0};
    ssize_t bytes_read = SimFS::read("/virtual_readme.md", buffer, sizeof(buffer) - 1, 0, &fi);
    
    EXPECT_GT(bytes_read, 0);
    
    std::string content(buffer);
    EXPECT_FALSE(content.empty());
    
    char buffer2[4096] = {0};
    ssize_t bytes_read2 = SimFS::read("/virtual_readme.md", buffer2, sizeof(buffer2) - 1, 0, &fi);
    EXPECT_EQ(bytes_read, bytes_read2);
    EXPECT_STREQ(buffer, buffer2);
}