#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include "simfs.h"
#include "db_manager.h"

// Test readdir functionality without mounting
int main() {
    std::cout << "Testing SimFS readdir functionality\n";
    std::cout << "===================================\n\n";
    
    // Create SimFS instance
    SimFS fs("./test_readdir.db", "http://localhost:8080/mock");
    SimFS::setInstance(&fs);
    
    // Create some test files and directories
    std::cout << "Creating test structure:\n";
    SimFS::mkdir("/documents", 0755);
    SimFS::mkdir("/images", 0755);
    
    struct fuse_file_info fi = {0};
    fi.flags = O_CREAT | O_RDWR;
    
    SimFS::create("/readme.txt", 0644, &fi);
    SimFS::create("/config.json", 0644, &fi);
    SimFS::create("/documents/report.pdf", 0644, &fi);
    SimFS::create("/documents/notes.txt", 0644, &fi);
    SimFS::create("/images/photo.jpg", 0644, &fi);
    
    // Test readdir on root
    std::cout << "\nListing root directory '/':\n";
    std::vector<std::string> entries;
    
    auto filler = [](void *buf, const char *name, const struct stat *stbuf,
                     off_t off, enum fuse_fill_dir_flags flags) -> int {
        (void)stbuf; (void)off; (void)flags;
        auto* vec = static_cast<std::vector<std::string>*>(buf);
        vec->push_back(name);
        return 0;
    };
    
    SimFS::readdir("/", &entries, filler, 0, nullptr, FUSE_READDIR_PLUS);
    
    for (const auto& entry : entries) {
        if (entry != "." && entry != "..") {
            std::cout << "  " << entry << "\n";
        }
    }
    
    // Test readdir on subdirectory
    std::cout << "\nListing /documents:\n";
    entries.clear();
    SimFS::readdir("/documents", &entries, filler, 0, nullptr, FUSE_READDIR_PLUS);
    
    for (const auto& entry : entries) {
        if (entry != "." && entry != "..") {
            std::cout << "  " << entry << "\n";
        }
    }
    
    // Test readdir on another subdirectory
    std::cout << "\nListing /images:\n";
    entries.clear();
    SimFS::readdir("/images", &entries, filler, 0, nullptr, FUSE_READDIR_PLUS);
    
    for (const auto& entry : entries) {
        if (entry != "." && entry != "..") {
            std::cout << "  " << entry << "\n";
        }
    }
    
    std::cout << "\nTest completed!\n";
    
    // Cleanup
    std::filesystem::remove_all("./test_readdir.db");
    
    return 0;
}