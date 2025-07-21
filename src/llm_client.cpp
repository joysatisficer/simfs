#include "llm_client.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <chrono>

using json = nlohmann::json;

// StreamingBuffer implementation
StreamingBuffer::StreamingBuffer() {}

StreamingBuffer::~StreamingBuffer() {}

void StreamingBuffer::appendData(const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.append(data);
    cv_.notify_all();
}

void StreamingBuffer::markComplete() {
    std::lock_guard<std::mutex> lock(mutex_);
    complete_ = true;
    cv_.notify_all();
}

void StreamingBuffer::markError(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = true;
    error_msg_ = error;
    complete_ = true;
    cv_.notify_all();
}

size_t StreamingBuffer::readData(char* buf, size_t size, off_t offset) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // For streaming behavior: if we're at the current end of buffer and not complete,
    // wait for more data. This makes reads blocking until data arrives.
    while (offset == static_cast<off_t>(buffer_.size()) && !complete_) {
        cv_.wait_for(lock, std::chrono::milliseconds(100));
    }
    
    // If still no data after completion, return EOF
    if (offset >= static_cast<off_t>(buffer_.size())) {
        return 0; // EOF
    }
    
    size_t available = buffer_.size() - offset;
    size_t to_read = std::min(size, available);
    
    if (to_read > 0) {
        memcpy(buf, buffer_.c_str() + offset, to_read);
    }
    
    return to_read;
}

bool StreamingBuffer::isComplete() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return complete_;
}

bool StreamingBuffer::hasError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

std::string StreamingBuffer::getError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_msg_;
}

size_t StreamingBuffer::getTotalSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

// LLMClient implementation
class LLMClient::Impl {
public:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
        size_t totalSize = size * nmemb;
        output->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }
    
    struct StreamContext {
        std::shared_ptr<StreamingBuffer> buffer;
        std::string accumulated_data;
    };
    
    static size_t StreamCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t totalSize = size * nmemb;
        StreamContext* ctx = static_cast<StreamContext*>(userp);
        
        ctx->accumulated_data.append(static_cast<char*>(contents), totalSize);
        
        // Process SSE chunks
        size_t pos = 0;
        while ((pos = ctx->accumulated_data.find("\n\n")) != std::string::npos) {
            std::string chunk = ctx->accumulated_data.substr(0, pos);
            ctx->accumulated_data.erase(0, pos + 2);
            
            if (chunk.find("data: ") == 0) {
                std::string data = chunk.substr(6);
                
                if (data == "[DONE]") {
                    ctx->buffer->markComplete();
                    continue;
                }
                
                try {
                    json event = json::parse(data);
                    if (event.contains("choices") && !event["choices"].empty()) {
                        auto& choice = event["choices"][0];
                        if (choice.contains("delta") && choice["delta"].contains("content")) {
                            std::string content = choice["delta"]["content"];
                            ctx->buffer->appendData(content);
                        }
                    }
                } catch (const std::exception& e) {
                    // Ignore parse errors for now
                }
            }
        }
        
        return totalSize;
    }
};

LLMClient::LLMClient(const std::string& endpoint) 
    : endpoint_(endpoint), pImpl(std::make_unique<Impl>()) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

LLMClient::~LLMClient() {
    curl_global_cleanup();
}

std::string LLMClient::generateFileContent(
    const std::string& file_path,
    const std::vector<FileContext>& folder_context,
    const std::vector<std::string>& recent_files) {
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }

    std::string response;
    
    try {
        json request_body;
        
        std::stringstream prompt;
        prompt << "Generate content for the file: " << file_path << "\n\n";
        
        if (!folder_context.empty()) {
            prompt << "Files in the same folder:\n";
            for (const auto& ctx : folder_context) {
                prompt << "- " << ctx.path << " (preview):\n";
                prompt << ctx.content.substr(0, 200) << "...\n\n";
            }
        }
        
        if (!recent_files.empty()) {
            prompt << "\nRecently accessed files:\n";
            for (const auto& file : recent_files) {
                prompt << "- " << file << "\n";
            }
        }
        
        prompt << "\nGenerate only the raw file content for " << file_path << ". No explanations or markdown.";
        
        request_body["model"] = "gpt-3.5-turbo";
        request_body["messages"] = json::array({
            {{"role", "system"}, {"content", "You are a file content generator. Generate ONLY the raw file content without any explanation, commentary, or markdown formatting. Do not include phrases like 'Here is the content' or 'Based on the context'. Start directly with the actual file content."}},
            {{"role", "user"}, {"content", prompt.str()}}
        });
        request_body["temperature"] = 0.7;
        request_body["max_tokens"] = 2048;
        
        std::string request_str = request_body.dump();
        
        curl_easy_setopt(curl, CURLOPT_URL, endpoint_.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request_str.length());
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        const char* api_key = std::getenv("OPENAI_API_KEY");
        if (api_key) {
            std::string auth_header = "Authorization: Bearer " + std::string(api_key);
            headers = curl_slist_append(headers, auth_header.c_str());
        }
        
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Impl::WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        
        CURLcode res = curl_easy_perform(curl);
        
        curl_slist_free_all(headers);
        
        if (res != CURLE_OK) {
            throw std::runtime_error("CURL request failed: " + std::string(curl_easy_strerror(res)));
        }
        
        json response_json = json::parse(response);
        
        if (response_json.contains("error")) {
            throw std::runtime_error("API error: " + response_json["error"]["message"].get<std::string>());
        }
        
        std::string content = response_json["choices"][0]["message"]["content"].get<std::string>();
        
        curl_easy_cleanup(curl);
        return content;
        
    } catch (...) {
        curl_easy_cleanup(curl);
        throw;
    }
}

std::shared_ptr<StreamingBuffer> LLMClient::generateFileContentStream(
    const std::string& file_path,
    const std::vector<FileContext>& folder_context,
    const std::vector<std::string>& recent_files) {
    
    auto buffer = std::make_shared<StreamingBuffer>();
    
    // Run the streaming request in a separate thread
    std::thread([this, file_path, folder_context, recent_files, buffer]() {
        CURL* curl = curl_easy_init();
        if (!curl) {
            buffer->markError("Failed to initialize CURL");
            return;
        }
        
        try {
            json request_body;
            
            std::stringstream prompt;
            prompt << "Generate content for the file: " << file_path << "\n\n";
            
            if (!folder_context.empty()) {
                prompt << "Files in the same folder:\n";
                for (const auto& ctx : folder_context) {
                    prompt << "- " << ctx.path << " (preview):\n";
                    prompt << ctx.content.substr(0, 200) << "...\n\n";
                }
            }
            
            if (!recent_files.empty()) {
                prompt << "\nRecently accessed files:\n";
                for (const auto& file : recent_files) {
                    prompt << "- " << file << "\n";
                }
            }
            
            prompt << "\nPlease generate appropriate content for " << file_path;
            prompt << " based on the context. The content should be realistic and consistent ";
            prompt << "with what would be expected in this file system location.";
            
            request_body["model"] = "gpt-3.5-turbo";
            request_body["messages"] = json::array({
                {{"role", "system"}, {"content", "You are a file content generator. Generate ONLY the raw file content without any explanation, commentary, or markdown formatting. Do not include phrases like 'Here is the content' or 'Based on the context'. Start directly with the actual file content."}},
                {{"role", "user"}, {"content", prompt.str()}}
            });
            request_body["temperature"] = 0.7;
            request_body["max_tokens"] = 2048;
            request_body["stream"] = true;  // Enable streaming
            
            std::string request_str = request_body.dump();
            
            curl_easy_setopt(curl, CURLOPT_URL, endpoint_.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request_str.length());
            
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, "Accept: text/event-stream");
            
            const char* api_key = std::getenv("OPENAI_API_KEY");
            if (api_key) {
                std::string auth_header = "Authorization: Bearer " + std::string(api_key);
                headers = curl_slist_append(headers, auth_header.c_str());
            }
            
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            
            Impl::StreamContext ctx;
            ctx.buffer = buffer;
            
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Impl::StreamCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
            
            CURLcode res = curl_easy_perform(curl);
            
            curl_slist_free_all(headers);
            
            if (res != CURLE_OK) {
                buffer->markError("CURL request failed: " + std::string(curl_easy_strerror(res)));
            } else if (!buffer->isComplete()) {
                buffer->markComplete();
            }
            
            curl_easy_cleanup(curl);
            
        } catch (const std::exception& e) {
            curl_easy_cleanup(curl);
            buffer->markError(e.what());
        }
    }).detach();
    
    return buffer;
}