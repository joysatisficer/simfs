# SimFS - Simulated File System with LLM Generation

SimFS is a lazily evaluated FUSE filesystem that generates file contents using language model inference. Files are generated on first access and persisted in RocksDB for subsequent reads.

## Features

- Lazy file generation using LLM when files are first accessed
- Persistence with RocksDB for consistent file contents
- Context-aware generation based on folder contents and recently accessed files
- Full FUSE filesystem support (read, write, create, delete)
- Comprehensive test suite

## Building

### Prerequisites

- CMake 3.14+
- C++17 compiler
- FUSE 3
- RocksDB
- libcurl
- nlohmann/json
- Google Test (for tests)
- OpenAI API key (or custom LLM endpoint)

### Build Instructions

```bash
cd simfs
mkdir build && cd build
cmake ..
make
```

### Running Tests

```bash
cd build
ctest
```

## Usage

```bash
# Set your OpenAI API key
export OPENAI_API_KEY=your_api_key_here

# Create a mount point
mkdir ~/simfs_mount

# Mount the filesystem
./simfs ~/simfs_mount

# Optional: Run in foreground with debug output
./simfs ~/simfs_mount -f -d

# Custom database and LLM endpoint
./simfs ~/simfs_mount --db-path=/path/to/db --llm-endpoint=http://localhost:8080/v1/chat/completions
```

## How It Works

1. When a file is accessed for the first time, SimFS generates its content using the configured LLM
2. The generation is conditioned on:
   - The file path
   - Contents of other files in the same directory
   - Recently accessed files
3. Generated content is stored in RocksDB for persistence
4. Subsequent accesses return the stored content without regeneration

## API

SimFS implements standard FUSE operations:
- `getattr` - Get file attributes
- `readdir` - List directory contents
- `open` - Open file
- `read` - Read file contents (triggers generation if needed)
- `write` - Write to file
- `create` - Create new file
- `unlink` - Delete file
- `mkdir` - Create directory
- `rmdir` - Remove directory

## Testing

The project includes three test suites:
- `test_db_manager` - Tests for RocksDB persistence layer
- `test_llm_client` - Tests for LLM client integration
- `test_simfs_integration` - Integration tests for FUSE operations