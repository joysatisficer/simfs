#include "simfs.h"
#include "db_manager.h"
#include "llm_client.h"
#include <cstring>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <deque>
#include <iostream>
#include <fstream>
#include <toml++/toml.hpp>
#include <unordered_set>

SimFS* SimFS::instance_ = nullptr;

struct fuse_operations SimFS::operations_ = {
    .getattr = SimFS::getattr,
    .mkdir = SimFS::mkdir,
    .unlink = SimFS::unlink,
    .rmdir = SimFS::rmdir,
    .open = SimFS::open,
    .read = SimFS::read,
    .write = SimFS::write,
    .readdir = SimFS::readdir,
    .create = SimFS::create,
};

static std::deque<std::string> recent_access_queue;
static std::mutex recent_access_mutex;
static const size_t MAX_RECENT_FILES = 10;

SimFS::SimFS(const std::string& db_path, const std::string& llm_endpoint) 
    : db_(std::make_unique<DBManager>(db_path)),
      llm_client_(std::make_unique<LLMClient>(llm_endpoint)) {
}

SimFS::~SimFS() = default;

struct fuse_operations* SimFS::getOperations() {
    return &operations_;
}

int SimFS::getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    
    memset(stbuf, 0, sizeof(struct stat));
    
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }
    
    SimFS* self = getInstance();
    std::lock_guard<std::mutex> lock(self->mutex_);
    
    std::string metadata_key = std::string("meta:") + path;
    std::string metadata;
    
    if (self->db_->get(metadata_key, metadata)) {
        bool is_dir = metadata.find("type:dir") != std::string::npos;
        size_t size = 0;
        
        if (!is_dir) {
            std::string content_key = std::string("content:") + path;
            std::string content;
            if (self->db_->get(content_key, content)) {
                size = content.length();
            }
        }
        
        stbuf->st_mode = is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
        stbuf->st_nlink = is_dir ? 2 : 1;
        stbuf->st_size = size;
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL);
        
        return 0;
    }
    
    // Special handling for special files - they must actually exist
    if (isSpecialFile(path)) {
        return -ENOENT;
    }
    
    // For lazy generation, only assume files (with extensions) exist
    std::string path_str(path);
    size_t last_slash = path_str.find_last_of('/');
    size_t last_dot = path_str.find_last_of('.');
    
    // Check if it has an extension (dot after last slash)
    if (last_dot != std::string::npos && 
        (last_slash == std::string::npos || last_dot > last_slash)) {
        // It's a file with an extension - report it exists for lazy generation
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = 0;  // Unknown size for streaming behavior
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL);
        return 0;
    }
    
    // Not a file with extension - doesn't exist
    return -ENOENT;
}

int SimFS::readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                   off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;
    
    filler(buf, ".", NULL, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", NULL, 0, static_cast<fuse_fill_dir_flags>(0));
    
    SimFS* self = getInstance();
    std::lock_guard<std::mutex> lock(self->mutex_);
    
    std::string dir_path = std::string(path);
    if (dir_path.back() != '/') {
        dir_path += '/';
    }
    
    auto entries = self->getDirectoryContents(dir_path);
    
    for (const auto& entry : entries) {
        size_t pos = entry.find_last_of('/');
        std::string name = (pos != std::string::npos) ? entry.substr(pos + 1) : entry;
        if (!name.empty()) {
            filler(buf, name.c_str(), NULL, 0, static_cast<fuse_fill_dir_flags>(0));
        }
    }
    
    return 0;
}

int SimFS::open(const char *path, struct fuse_file_info *fi) {
    // Always allow opening files - they will be generated on first read if needed
    // Use direct I/O to prevent caching for streaming behavior
    fi->direct_io = 1;
    fi->nonseekable = 1;
    return 0;
}

int SimFS::read(const char *path, char *buf, size_t size, off_t offset,
                struct fuse_file_info *fi) {
    (void) fi;
    
    std::cerr << "[DEBUG] read() called for: " << path << " offset: " << offset << " size: " << size << std::endl;
    
    SimFS* self = getInstance();
    
    // Check if we have a streaming buffer for this file
    std::shared_ptr<StreamingBuffer> stream_buffer;
    {
        std::lock_guard<std::mutex> stream_lock(self->streaming_mutex_);
        auto it = self->streaming_buffers_.find(path);
        if (it != self->streaming_buffers_.end()) {
            stream_buffer = it->second;
        }
    }
    
    if (stream_buffer) {
        // Use streaming buffer
        std::cerr << "[DEBUG] Using streaming buffer for: " << path << std::endl;
        size_t bytes_read = stream_buffer->readData(buf, size, offset);
        
        // If streaming is complete, save to database and cleanup
        if (stream_buffer->isComplete() && offset >= static_cast<off_t>(stream_buffer->getTotalSize())) {
            // Get full content from buffer
            size_t total_size = stream_buffer->getTotalSize();
            if (total_size > 0) {
                std::vector<char> full_content(total_size);
                stream_buffer->readData(full_content.data(), total_size, 0);
                
                std::string content_key = std::string("content:") + path;
                self->db_->put(content_key, std::string(full_content.data(), total_size));
                
                std::string metadata_key = std::string("meta:") + path;
                self->db_->put(metadata_key, "type:file");
                
                // Add to recent access queue since we just generated it
                {
                    std::lock_guard<std::mutex> recent_lock(recent_access_mutex);
                    recent_access_queue.push_back(path);
                    if (recent_access_queue.size() > MAX_RECENT_FILES) {
                        recent_access_queue.pop_front();
                    }
                }
            }
            
            // Remove streaming buffer
            std::lock_guard<std::mutex> stream_lock(self->streaming_mutex_);
            self->streaming_buffers_.erase(path);
        }
        
        return bytes_read;
    }
    
    // Check if content exists in database
    std::string content_key = std::string("content:") + path;
    std::string content;
    
    std::unique_lock<std::mutex> lock(self->mutex_);
    
    if (self->db_->get(content_key, content)) {
        // Content exists in database
        std::cerr << "[DEBUG] Content found in DB, length: " << content.length() << std::endl;
    } else {
        // Special handling for special files - never generate them
        if (isSpecialFile(path)) {
            std::cerr << "[DEBUG] Special file not found, returning empty: " << path << std::endl;
            return 0;  // EOF for non-existent special files
        }
        
        // Check if someone else is already streaming this file
        {
            std::lock_guard<std::mutex> stream_lock(self->streaming_mutex_);
            auto it = self->streaming_buffers_.find(path);
            if (it != self->streaming_buffers_.end()) {
                // Another reader is streaming this file - use their buffer
                std::cerr << "[DEBUG] Joining existing stream for: " << path << std::endl;
                stream_buffer = it->second;
                lock.unlock();  // Release main lock before blocking read
                return stream_buffer->readData(buf, size, offset);
            }
        }
        
        // Start streaming generation
        std::cerr << "[DEBUG] Starting streaming generation for: " << path << std::endl;
        
        std::string dir_path = std::string(path).substr(0, std::string(path).find_last_of('/'));
        auto files = self->getDirectoryContents(dir_path);
        
        std::vector<FileContext> context_files;
        for (const auto& file : files) {
            std::string metadata_key = std::string("meta:") + file;
            std::string metadata;
            
            if (self->db_->get(metadata_key, metadata) && metadata.find("type:file") != std::string::npos) {
                std::string content_key = std::string("content:") + file;
                std::string file_content;
                
                if (self->db_->get(content_key, file_content)) {
                    FileContext fc;
                    fc.path = file;
                    fc.content = file_content.substr(0, 200) + "...";
                    context_files.push_back(fc);
                }
            }
        }
        
        std::vector<std::string> recent_paths;
        {
            std::lock_guard<std::mutex> recent_lock(recent_access_mutex);
            recent_paths.assign(recent_access_queue.begin(), recent_access_queue.end());
        }
        
        // Build exclusion list from folder context files and the file being generated
        std::vector<std::string> exclude_paths;
        exclude_paths.push_back(path);  // Exclude the file being generated
        for (const auto& ctx : context_files) {
            exclude_paths.push_back(ctx.path);
        }
        
        // Get recent files with content, excluding folder context files
        std::vector<FileContext> recent_files = self->getRecentFilesWithContent(recent_paths, exclude_paths);
        
        // Get the configuration for this path
        DirectoryConfig config = self->getConfigForPath(path);
        
        auto buffer = self->llm_client_->generateFileContentStream(path, context_files, recent_files, config.model_name);
        
        {
            std::lock_guard<std::mutex> stream_lock(self->streaming_mutex_);
            self->streaming_buffers_[path] = buffer;
        }
        
        // Add to recent access queue since we're generating it
        {
            std::lock_guard<std::mutex> recent_lock(recent_access_mutex);
            recent_access_queue.push_back(path);
            if (recent_access_queue.size() > MAX_RECENT_FILES) {
                recent_access_queue.pop_front();
            }
        }
        
        // Return data from the new streaming buffer
        return buffer->readData(buf, size, offset);
    }
    
    // Update recent access
    {
        std::lock_guard<std::mutex> recent_lock(recent_access_mutex);
        recent_access_queue.push_back(path);
        if (recent_access_queue.size() > MAX_RECENT_FILES) {
            recent_access_queue.pop_front();
        }
    }
    
    // Return data from database content
    size_t len = content.length();
    if (offset < static_cast<off_t>(len)) {
        if (offset + size > len) {
            size = len - offset;
        }
        memcpy(buf, content.c_str() + offset, size);
    } else {
        size = 0;
    }
    
    return size;
}

int SimFS::write(const char *path, const char *buf, size_t size, off_t offset,
                 struct fuse_file_info *fi) {
    (void) fi;
    
    SimFS* self = getInstance();
    std::lock_guard<std::mutex> lock(self->mutex_);
    
    std::string content_key = std::string("content:") + path;
    std::string content;
    
    if (!self->db_->get(content_key, content)) {
        content = "";
    }
    
    if (offset + size > content.length()) {
        content.resize(offset + size, '\0');
    }
    
    memcpy(&content[offset], buf, size);
    
    self->db_->put(content_key, content);
    
    // If this is a config file, clear the config cache
    if (isSpecialFile(path)) {
        std::string path_str(path);
        if (path_str.find(".simfs_config.toml") != std::string::npos) {
            std::lock_guard<std::mutex> config_lock(self->config_mutex_);
            self->config_cache_.clear();
            std::cerr << "[INFO] Config cache cleared due to config file update: " << path << std::endl;
        }
    }
    
    return size;
}

int SimFS::create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) mode;
    (void) fi;
    
    SimFS* self = getInstance();
    std::lock_guard<std::mutex> lock(self->mutex_);
    
    std::string metadata_key = std::string("meta:") + path;
    std::string content_key = std::string("content:") + path;
    
    self->db_->put(metadata_key, "type:file");
    self->db_->put(content_key, "");
    
    return 0;
}

int SimFS::unlink(const char *path) {
    SimFS* self = getInstance();
    std::lock_guard<std::mutex> lock(self->mutex_);
    
    std::string metadata_key = std::string("meta:") + path;
    std::string content_key = std::string("content:") + path;
    
    self->db_->remove(metadata_key);
    self->db_->remove(content_key);
    
    // If this is a config file, clear the config cache
    if (isSpecialFile(path)) {
        std::string path_str(path);
        if (path_str.find(".simfs_config.toml") != std::string::npos) {
            std::lock_guard<std::mutex> config_lock(self->config_mutex_);
            self->config_cache_.clear();
            std::cerr << "[INFO] Config cache cleared due to config file deletion: " << path << std::endl;
        }
    }
    
    return 0;
}

int SimFS::mkdir(const char *path, mode_t mode) {
    (void) mode;
    
    SimFS* self = getInstance();
    std::lock_guard<std::mutex> lock(self->mutex_);
    
    std::string metadata_key = std::string("meta:") + path;
    self->db_->put(metadata_key, "type:dir");
    
    return 0;
}

int SimFS::rmdir(const char *path) {
    SimFS* self = getInstance();
    std::lock_guard<std::mutex> lock(self->mutex_);
    
    std::string metadata_key = std::string("meta:") + path;
    self->db_->remove(metadata_key);
    
    return 0;
}

std::string SimFS::generateContent(const std::string& path) {
    // Safety check: never generate special files
    if (isSpecialFile(path)) {
        return "";
    }
    
    std::string dir_path = path.substr(0, path.find_last_of('/'));
    auto files = getDirectoryContents(dir_path);
    
    std::vector<FileContext> context_files;
    for (const auto& file : files) {
        std::string metadata_key = std::string("meta:") + file;
        std::string metadata;
        
        if (db_->get(metadata_key, metadata) && metadata.find("type:file") != std::string::npos) {
            std::string content_key = std::string("content:") + file;
            std::string content;
            
            if (db_->get(content_key, content)) {
                FileContext fc;
                fc.path = file;
                fc.content = content.substr(0, 200) + "...";
                context_files.push_back(fc);
            }
        }
    }
    
    std::vector<std::string> recent_paths;
    {
        std::lock_guard<std::mutex> recent_lock(recent_access_mutex);
        recent_paths.assign(recent_access_queue.begin(), recent_access_queue.end());
    }
    
    // Build exclusion list from folder context files and the file being generated
    std::vector<std::string> exclude_paths;
    exclude_paths.push_back(path);  // Exclude the file being generated
    for (const auto& ctx : context_files) {
        exclude_paths.push_back(ctx.path);
    }
    
    // Get recent files with content, excluding folder context files
    std::vector<FileContext> recent_files = getRecentFilesWithContent(recent_paths, exclude_paths);
    
    // Get the configuration for this path
    DirectoryConfig config = getConfigForPath(path);
    
    try {
        return llm_client_->generateFileContent(path, context_files, recent_files, config.model_name);
    } catch (const std::exception& e) {
        // If LLM generation fails, return a placeholder message
        return "Error generating content: " + std::string(e.what()) + "\n";
    }
}

std::string SimFS::getFileContent(const std::string& path) {
    std::cerr << "[DEBUG] getFileContent called for: " << path << std::endl;
    std::string content_key = std::string("content:") + path;
    std::string content;
    
    if (!db_->get(content_key, content)) {
        // Never generate special files
        if (isSpecialFile(path)) {
            std::cerr << "[DEBUG] Special file requested but not found: " << path << std::endl;
            return "";
        }
        
        std::cerr << "[DEBUG] Content not in DB, generating..." << std::endl;
        content = generateContent(path);
        std::cerr << "[DEBUG] Generated content length: " << content.length() << std::endl;
        db_->put(content_key, content);
        
        std::string metadata_key = std::string("meta:") + path;
        db_->put(metadata_key, "type:file");
        
        // Add to recent access queue since we just generated it
        {
            std::lock_guard<std::mutex> recent_lock(recent_access_mutex);
            recent_access_queue.push_back(path);
            if (recent_access_queue.size() > MAX_RECENT_FILES) {
                recent_access_queue.pop_front();
            }
        }
    } else {
        std::cerr << "[DEBUG] Content found in DB, length: " << content.length() << std::endl;
    }
    
    return content;
}

bool SimFS::fileExists(const std::string& path) {
    std::string metadata_key = std::string("meta:") + path;
    return db_->exists(metadata_key);
}

std::vector<std::string> SimFS::getDirectoryContents(const std::string& path) {
    std::vector<std::string> contents;
    std::string prefix = "meta:" + path;
    
    auto keys = db_->listKeys(prefix);
    
    for (const auto& key : keys) {
        std::string file_path = key.substr(5);
        
        if (file_path.substr(0, path.length()) == path) {
            std::string relative_path = file_path.substr(path.length());
            
            size_t next_slash = relative_path.find('/');
            if (next_slash == std::string::npos || next_slash == relative_path.length() - 1) {
                contents.push_back(file_path);
            }
        }
    }
    
    return contents;
}

std::string SimFS::getFolderContext(const std::string& path) {
    std::stringstream context;
    auto files = getDirectoryContents(path);
    
    std::vector<std::pair<std::string, std::string>> file_contents;
    
    for (const auto& file : files) {
        std::string metadata_key = std::string("meta:") + file;
        std::string metadata;
        
        if (db_->get(metadata_key, metadata) && metadata.find("type:file") != std::string::npos) {
            std::string content_key = std::string("content:") + file;
            std::string content;
            
            if (db_->get(content_key, content)) {
                file_contents.push_back(std::make_pair(file, content));
            }
        }
    }
    
    return context.str();
}

DirectoryConfig SimFS::loadConfigFromDirectory(const std::string& dir_path) {
    DirectoryConfig config;
    
    // Construct the config file path
    std::string config_path;
    if (dir_path.empty() || dir_path == "/") {
        config_path = "/.simfs_config.toml";
    } else {
        config_path = dir_path;
        if (config_path.back() != '/') {
            config_path += '/';
        }
        config_path += ".simfs_config.toml";
    }
    
    std::cerr << "[DEBUG] Looking for config file: " << config_path << std::endl;
    
    // Check if the config file exists in the virtual filesystem
    std::string content_key = std::string("content:") + config_path;
    std::string config_content;
    
    if (db_->get(content_key, config_content)) {
        std::cerr << "[INFO] Found config file: " << config_path << " with content: " << config_content << std::endl;
        // Parse the TOML content
        try {
            auto table = toml::parse(config_content);
            
            // Read model name if present
            if (table.contains("model")) {
                config.model_name = table["model"].value_or(config.model_name);
                std::cerr << "[INFO] Loaded model from config: " << config.model_name << std::endl;
            }
            
            // Future: read other settings
            // if (table.contains("temperature")) {
            //     config.temperature = table["temperature"].value_or(config.temperature);
            // }
            
        } catch (const toml::parse_error& e) {
            std::cerr << "[WARNING] Failed to parse config file " << config_path 
                      << ": " << e.what() << std::endl;
        }
    } else {
        std::cerr << "[DEBUG] No config file found at: " << config_path << std::endl;
    }
    
    return config;
}

DirectoryConfig SimFS::getConfigForPath(const std::string& path) {
    std::cerr << "[DEBUG] getConfigForPath called for: " << path << std::endl;
    
    // Extract the directory from the path
    size_t last_slash = path.find_last_of('/');
    std::string dir_path = (last_slash != std::string::npos) ? path.substr(0, last_slash) : "";
    
    std::cerr << "[DEBUG] Extracted directory path: " << dir_path << std::endl;
    
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        auto it = config_cache_.find(dir_path);
        if (it != config_cache_.end()) {
            std::cerr << "[DEBUG] Found config in cache for: " << dir_path << " with model: " << it->second.model_name << std::endl;
            return it->second;
        }
    }
    
    // Load config, checking parent directories for inheritance
    DirectoryConfig merged_config;
    std::vector<std::string> dir_components;
    
    // Split the path into components
    if (!dir_path.empty()) {
        size_t start = 0;
        size_t end = dir_path.find('/', start);
        
        while (end != std::string::npos) {
            if (end > start) {
                dir_components.push_back(dir_path.substr(start, end - start));
            }
            start = end + 1;
            end = dir_path.find('/', start);
        }
        
        if (start < dir_path.length()) {
            dir_components.push_back(dir_path.substr(start));
        }
    }
    
    // Check each directory level for config, from root to target
    std::string current_path;
    for (const auto& component : dir_components) {
        current_path += "/" + component;
        
        DirectoryConfig dir_config = loadConfigFromDirectory(current_path);
        // For now, just override with the most specific config
        // In the future, we could merge settings more intelligently
        if (dir_config.model_name != "meta-llama/Llama-3.2-3B-Instruct") {
            merged_config = dir_config;
        }
    }
    
    // Also check the root directory
    DirectoryConfig root_config = loadConfigFromDirectory("/");
    if (merged_config.model_name == "meta-llama/Llama-3.2-3B-Instruct" && root_config.model_name != "meta-llama/Llama-3.2-3B-Instruct") {
        merged_config = root_config;
    }
    
    // Cache the result
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_cache_[dir_path] = merged_config;
    }
    
    std::cerr << "[DEBUG] Final config for path " << path << ": model=" << merged_config.model_name << std::endl;
    
    return merged_config;
}

bool SimFS::isSpecialFile(const std::string& path) {
    // Extract filename from path
    size_t last_slash = path.find_last_of('/');
    std::string filename = (last_slash != std::string::npos) ? path.substr(last_slash + 1) : path;
    
    // List of special files that should never be auto-generated
    static const std::vector<std::string> special_files = {
        ".simfs_config.toml",    // SimFS configuration
        ".xdg-volume-info",      // XDG volume metadata
        "autorun.inf",           // Windows autorun file
        ".DS_Store",             // macOS directory metadata
        "desktop.ini",           // Windows folder settings
        "Thumbs.db",             // Windows thumbnail cache
        ".directory",            // KDE directory settings
        "NTUSER.DAT",           // Windows user registry
        "pagefile.sys",          // Windows page file
        "hiberfil.sys",          // Windows hibernation file
        "swapfile.sys"           // Windows swap file
    };
    
    // Check if filename matches any special file
    for (const auto& special : special_files) {
        if (filename == special) {
            return true;
        }
    }
    
    return false;
}

std::string SimFS::getTailContent(const std::string& content, size_t max_chars) {
    if (content.length() <= max_chars) {
        return content;
    }
    // Return the last max_chars characters
    return content.substr(content.length() - max_chars);
}

std::vector<FileContext> SimFS::getRecentFilesWithContent(
    const std::vector<std::string>& recent_paths,
    const std::vector<std::string>& exclude_paths) {
    std::vector<FileContext> result;
    
    // Create a set for faster exclusion checking
    std::unordered_set<std::string> exclude_set(exclude_paths.begin(), exclude_paths.end());
    
    // Constants for token management
    const size_t CHARS_PER_TOKEN = 3;
    const size_t MAX_TOKENS_PER_FILE = 1200;
    const size_t MAX_CHARS_PER_FILE = MAX_TOKENS_PER_FILE * CHARS_PER_TOKEN; // 3600 chars
    const size_t MAX_RECENT_FILES = 6;
    const size_t MAX_TOTAL_TOKENS = 8000;
    const size_t MAX_TOTAL_CHARS = MAX_TOTAL_TOKENS * CHARS_PER_TOKEN; // 24000 chars
    
    // Take up to the last 6 files
    size_t start_idx = (recent_paths.size() > MAX_RECENT_FILES) 
        ? recent_paths.size() - MAX_RECENT_FILES 
        : 0;
    
    size_t total_chars = 0;
    
    for (size_t i = start_idx; i < recent_paths.size(); ++i) {
        const std::string& path = recent_paths[i];
        
        // Skip special files
        if (isSpecialFile(path)) {
            continue;
        }
        
        // Skip excluded files
        if (exclude_set.find(path) != exclude_set.end()) {
            continue;
        }
        
        std::string content_key = std::string("content:") + path;
        std::string content;
        
        if (db_->get(content_key, content)) {
            // Get the tail of the content, up to max chars per file
            std::string tail_content = getTailContent(content, MAX_CHARS_PER_FILE);
            
            // Check if adding this would exceed total limit
            if (total_chars + tail_content.length() > MAX_TOTAL_CHARS) {
                // Truncate to fit within total limit
                size_t remaining_chars = MAX_TOTAL_CHARS - total_chars;
                if (remaining_chars > 0) {
                    tail_content = tail_content.substr(0, remaining_chars);
                } else {
                    break; // No more room
                }
            }
            
            FileContext fc;
            fc.path = path;
            fc.content = tail_content;
            result.push_back(fc);
            
            total_chars += tail_content.length();
            
            // Stop if we've reached the total limit
            if (total_chars >= MAX_TOTAL_CHARS) {
                break;
            }
        }
    }
    
    return result;
}