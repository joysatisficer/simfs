#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <algorithm>

// Simplified standalone test without external dependencies

class SimpleDB {
private:
    std::map<std::string, std::string> data;

public:
    bool put(const std::string& key, const std::string& value) {
        data[key] = value;
        return true;
    }

    bool get(const std::string& key, std::string& value) {
        auto it = data.find(key);
        if (it != data.end()) {
            value = it->second;
            return true;
        }
        return false;
    }

    bool exists(const std::string& key) {
        return data.find(key) != data.end();
    }

    std::vector<std::string> listKeys(const std::string& prefix) {
        std::vector<std::string> keys;
        for (const auto& pair : data) {
            if (pair.first.find(prefix) == 0) {
                keys.push_back(pair.first);
            }
        }
        return keys;
    }
};

class SimpleLLM {
public:
    std::string generateContent(const std::string& path) {
        // Simple mock content generation
        std::stringstream ss;
        ss << "Generated content for: " << path << "\n";
        
        if (path.find(".txt") != std::string::npos) {
            ss << "This is a text file.\n";
            ss << "It contains sample text content.\n";
        } else if (path.find(".md") != std::string::npos) {
            ss << "# " << path << "\n\n";
            ss << "This is a markdown file with generated content.\n";
        } else if (path.find(".cpp") != std::string::npos) {
            ss << "#include <iostream>\n\n";
            ss << "int main() {\n";
            ss << "    std::cout << \"Generated C++ file\" << std::endl;\n";
            ss << "    return 0;\n";
            ss << "}\n";
        } else {
            ss << "Generic file content for " << path << "\n";
        }
        
        return ss.str();
    }
};

class SimpleFS {
private:
    std::unique_ptr<SimpleDB> db;
    std::unique_ptr<SimpleLLM> llm;

public:
    SimpleFS() : db(std::make_unique<SimpleDB>()), llm(std::make_unique<SimpleLLM>()) {}

    bool createFile(const std::string& path) {
        db->put("meta:" + path, "type:file");
        db->put("content:" + path, "");
        return true;
    }

    bool createDir(const std::string& path) {
        db->put("meta:" + path, "type:dir");
        return true;
    }

    std::string readFile(const std::string& path) {
        std::string content;
        if (!db->get("content:" + path, content)) {
            // Generate content if not exists
            content = llm->generateContent(path);
            db->put("content:" + path, content);
            db->put("meta:" + path, "type:file");
        }
        return content;
    }

    bool writeFile(const std::string& path, const std::string& content) {
        db->put("meta:" + path, "type:file");
        db->put("content:" + path, content);
        return true;
    }

    bool exists(const std::string& path) {
        return db->exists("meta:" + path);
    }

    std::vector<std::string> listDir(const std::string& path) {
        std::vector<std::string> entries;
        std::string prefix = "meta:" + path;
        if (path.back() != '/') prefix += '/';
        
        auto keys = db->listKeys(prefix);
        for (const auto& key : keys) {
            std::string entry = key.substr(5); // Remove "meta:"
            entries.push_back(entry);
        }
        return entries;
    }
};

// Test functions
void testBasicOperations() {
    std::cout << "=== Testing Basic Operations ===" << std::endl;
    
    SimpleFS fs;
    
    // Test file creation
    std::cout << "Creating file /test.txt" << std::endl;
    fs.createFile("/test.txt");
    
    // Test file write
    std::cout << "Writing to /test.txt" << std::endl;
    fs.writeFile("/test.txt", "Hello, SimFS!");
    
    // Test file read
    std::cout << "Reading /test.txt: ";
    std::string content = fs.readFile("/test.txt");
    std::cout << content << std::endl;
    
    // Test directory creation
    std::cout << "\nCreating directory /testdir" << std::endl;
    fs.createDir("/testdir");
    
    // Test lazy generation
    std::cout << "\nReading non-existent file /generated.md:" << std::endl;
    std::string generated = fs.readFile("/generated.md");
    std::cout << generated << std::endl;
    
    // Verify persistence
    std::cout << "Reading /generated.md again (should be same):" << std::endl;
    std::string generated2 = fs.readFile("/generated.md");
    std::cout << "Content matches: " << (generated == generated2 ? "YES" : "NO") << std::endl;
}

void testDirectoryListing() {
    std::cout << "\n=== Testing Directory Listing ===" << std::endl;
    
    SimpleFS fs;
    
    // Create some files
    fs.createFile("/file1.txt");
    fs.createFile("/file2.cpp");
    fs.createDir("/subdir");
    fs.createFile("/subdir/file3.md");
    
    // List root
    std::cout << "Files in /:" << std::endl;
    auto entries = fs.listDir("/");
    for (const auto& entry : entries) {
        std::cout << "  " << entry << std::endl;
    }
    
    // List subdirectory
    std::cout << "\nFiles in /subdir/:" << std::endl;
    entries = fs.listDir("/subdir");
    for (const auto& entry : entries) {
        std::cout << "  " << entry << std::endl;
    }
}

void testGeneratedContent() {
    std::cout << "\n=== Testing Generated Content ===" << std::endl;
    
    SimpleFS fs;
    
    // Test different file types
    std::vector<std::string> test_files = {
        "/readme.txt",
        "/index.md", 
        "/main.cpp"
    };
    
    for (const auto& file : test_files) {
        std::cout << "\nGenerating content for " << file << ":" << std::endl;
        std::string content = fs.readFile(file);
        std::cout << content << std::endl;
        std::cout << "---" << std::endl;
    }
}

int main() {
    std::cout << "SimFS Standalone Test Suite" << std::endl;
    std::cout << "===========================" << std::endl;
    
    testBasicOperations();
    testDirectoryListing();
    testGeneratedContent();
    
    std::cout << "\nAll tests completed!" << std::endl;
    
    return 0;
}