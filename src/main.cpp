#include "simfs.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " <mountpoint> [options]\n";
    std::cerr << "\nOptions:\n";
    std::cerr << "  --db-path=PATH       Path to RocksDB database (default: ./simfs.db)\n";
    std::cerr << "  --llm-endpoint=URL   LLM API endpoint (default: https://api.openai.com/v1/chat/completions)\n";
    std::cerr << "  -f                   Run in foreground\n";
    std::cerr << "  -d                   Enable debug output\n";
    std::cerr << "  -h                   Print this help message\n";
    std::cerr << "\nEnvironment variables:\n";
    std::cerr << "  OPENAI_API_KEY       API key for OpenAI (required for default endpoint)\n";
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string db_path = "./simfs.db";
    std::string llm_endpoint = "https://api.openai.com/v1/chat/completions";
    
    std::vector<char*> fuse_args;
    fuse_args.push_back(argv[0]);
    
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        
        if (arg.find("--db-path=") == 0) {
            db_path = arg.substr(10);
        } else if (arg.find("--llm-endpoint=") == 0) {
            llm_endpoint = arg.substr(15);
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            fuse_args.push_back(argv[i]);
        }
    }
    
    if (fuse_args.size() < 2) {
        std::cerr << "Error: No mountpoint specified\n";
        print_usage(argv[0]);
        return 1;
    }
    
    if (llm_endpoint.find("openai.com") != std::string::npos) {
        const char* api_key = std::getenv("OPENAI_API_KEY");
        if (!api_key) {
            std::cerr << "Error: OPENAI_API_KEY environment variable not set\n";
            std::cerr << "Please set your OpenAI API key or use a different endpoint\n";
            return 1;
        }
    }
    
    try {
        SimFS simfs(db_path, llm_endpoint);
        SimFS::setInstance(&simfs);
        
        std::cout << "Mounting SimFS at " << fuse_args[1] << "\n";
        std::cout << "Database: " << db_path << "\n";
        std::cout << "LLM endpoint: " << llm_endpoint << "\n";
        
        int ret = fuse_main(fuse_args.size(), fuse_args.data(), 
                           simfs.getOperations(), nullptr);
        
        return ret;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}