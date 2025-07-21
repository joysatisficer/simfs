#include <gtest/gtest.h>
#include "llm_client.h"
#include <cstdlib>

class LLMClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* endpoint = std::getenv("LLM_ENDPOINT");
        if (!endpoint) {
            endpoint = "https://api.openai.com/v1/chat/completions";
        }
        client_ = std::make_unique<LLMClient>(endpoint);
    }

    std::unique_ptr<LLMClient> client_;
};

TEST_F(LLMClientTest, GenerateSimpleFileContent) {
    const char* api_key = std::getenv("OPENAI_API_KEY");
    if (!api_key) {
        GTEST_SKIP() << "OPENAI_API_KEY not set, skipping test";
    }

    std::vector<FileContext> folder_context;
    std::vector<std::string> recent_files;
    
    std::string content = client_->generateFileContent(
        "/home/user/test.txt",
        folder_context,
        recent_files
    );
    
    EXPECT_FALSE(content.empty());
    EXPECT_GT(content.length(), 10);
}

TEST_F(LLMClientTest, GenerateWithContext) {
    const char* api_key = std::getenv("OPENAI_API_KEY");
    if (!api_key) {
        GTEST_SKIP() << "OPENAI_API_KEY not set, skipping test";
    }

    std::vector<FileContext> folder_context = {
        {"/home/user/main.cpp", "#include <iostream>\nint main() { std::cout << \"Hello, World!\" << std::endl; return 0; }"},
        {"/home/user/CMakeLists.txt", "cmake_minimum_required(VERSION 3.10)\nproject(MyProject)\n"}
    };
    
    std::vector<std::string> recent_files = {
        "/home/user/README.md",
        "/home/user/src/utils.h"
    };
    
    std::string content = client_->generateFileContent(
        "/home/user/Makefile",
        folder_context,
        recent_files
    );
    
    EXPECT_FALSE(content.empty());
}

TEST_F(LLMClientTest, MockedResponse) {
    setenv("LLM_ENDPOINT", "http://localhost:8080/mock", 1);
    
    LLMClient mock_client("http://localhost:8080/mock");
    
    std::vector<FileContext> folder_context;
    std::vector<std::string> recent_files;
    
    try {
        std::string content = mock_client.generateFileContent(
            "/test/file.txt",
            folder_context,
            recent_files
        );
    } catch (const std::exception& e) {
        std::string error_msg = e.what();
        EXPECT_TRUE(error_msg.find("CURL") != std::string::npos || 
                    error_msg.find("API") != std::string::npos);
    }
}