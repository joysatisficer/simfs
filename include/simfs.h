#ifndef SIMFS_H
#define SIMFS_H

#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

class DBManager;
class LLMClient;
class StreamingBuffer;

class SimFS {
public:
    SimFS(const std::string& db_path, const std::string& llm_endpoint);
    ~SimFS();

    static int getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
    static int readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags);
    static int open(const char *path, struct fuse_file_info *fi);
    static int read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi);
    static int write(const char *path, const char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi);
    static int create(const char *path, mode_t mode, struct fuse_file_info *fi);
    static int unlink(const char *path);
    static int mkdir(const char *path, mode_t mode);
    static int rmdir(const char *path);

    static void setInstance(SimFS* instance) { instance_ = instance; }
    static SimFS* getInstance() { return instance_; }

    struct fuse_operations* getOperations();

private:
    std::string generateContent(const std::string& path);
    std::string getFileContent(const std::string& path);
    bool fileExists(const std::string& path);
    std::vector<std::string> getDirectoryContents(const std::string& path);
    std::string getFolderContext(const std::string& path);

    std::unique_ptr<DBManager> db_;
    std::unique_ptr<LLMClient> llm_client_;
    mutable std::mutex mutex_;
    
    // Streaming support
    mutable std::mutex streaming_mutex_;
    mutable std::unordered_map<std::string, std::shared_ptr<StreamingBuffer>> streaming_buffers_;
    
    static SimFS* instance_;
    static struct fuse_operations operations_;
};

#endif