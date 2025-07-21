cmake_minimum_required(VERSION 3.14)
project(simfs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

find_package(RocksDB REQUIRED)

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(simfs
    src/main.cpp
    src/simfs.cpp
    src/llm_client.cpp
    src/db_manager.cpp
)

enable_testing()
find_package(GTest REQUIRED)

add_executable(test_db_manager
    tests/test_db_manager.cpp
    src/db_manager.cpp
)

target_include_directories(test_db_manager PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_db_manager
    GTest::gtest_main
    RocksDB::rocksdb
    pthread
)

add_test(NAME test_db_manager COMMAND test_db_manager)

add_executable(test_llm_client
    tests/test_llm_client.cpp
    src/llm_client.cpp
)

target_include_directories(test_llm_client PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_llm_client
    GTest::gtest_main
    CURL::libcurl
    nlohmann_json::nlohmann_json
    pthread
)

add_test(NAME test_llm_client COMMAND test_llm_client)

add_executable(test_simfs_integration
    tests/test_simfs_integration.cpp
    src/simfs.cpp
    src/db_manager.cpp
    src/llm_client.cpp
)

target_include_directories(test_simfs_integration PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${FUSE3_INCLUDE_DIRS}
)

target_link_libraries(test_simfs_integration
    GTest::gtest_main
    ${FUSE3_LIBRARIES}
    RocksDB::rocksdb
    CURL::libcurl
    nlohmann_json::nlohmann_json
    pthread
)

target_compile_options(test_simfs_integration PRIVATE ${FUSE3_CFLAGS_OTHER})

add_test(NAME test_simfs_integration COMMAND test_simfs_integration)

target_include_directories(simfs PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${FUSE3_INCLUDE_DIRS}
)

target_link_libraries(simfs
    ${FUSE3_LIBRARIES}
    RocksDB::rocksdb
    CURL::libcurl
    nlohmann_json::nlohmann_json
    pthread
)

target_compile_options(simfs PRIVATE ${FUSE3_CFLAGS_OTHER})