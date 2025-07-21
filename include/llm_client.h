#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

struct FileContext {
    std::string path;
    std::string content;
};

// Streaming buffer for progressive content delivery
class StreamingBuffer {
public:
    StreamingBuffer();
    ~StreamingBuffer();
    
    // Producer side - called by LLM streaming callback
    void appendData(const std::string& data);
    void markComplete();
    void markError(const std::string& error);
    
    // Consumer side - called by FUSE read operations
    size_t readData(char* buf, size_t size, off_t offset);
    bool isComplete() const;
    bool hasError() const;
    std::string getError() const;
    size_t getTotalSize() const;
    
private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    std::string buffer_;
    bool complete_ = false;
    bool error_ = false;
    std::string error_msg_;
};

class LLMClient {
public:
    LLMClient(const std::string& endpoint);
    ~LLMClient();

    // Original blocking API (kept for compatibility)
    std::string generateFileContent(
        const std::string& file_path,
        const std::vector<FileContext>& folder_context,
        const std::vector<FileContext>& recent_files,
        const std::string& model_name = "meta-llama/Llama-3.2-3B-Instruct"
    );
    
    // New streaming API
    std::shared_ptr<StreamingBuffer> generateFileContentStream(
        const std::string& file_path,
        const std::vector<FileContext>& folder_context,
        const std::vector<FileContext>& recent_files,
        const std::string& model_name = "meta-llama/Llama-3.2-3B-Instruct"
    );

private:
    std::string endpoint_;
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

#endif