cmake_minimum_required(VERSION 3.20)
project(mini_lsm_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Sanitizer options
option(ASAN "Enable AddressSanitizer" OFF)
option(TSAN "Enable ThreadSanitizer" OFF)

if(ASAN AND TSAN)
    message(FATAL_ERROR "ASAN and TSAN cannot be enabled simultaneously")
endif()

set(SANITIZER_FLAGS "")
if(ASAN)
    set(SANITIZER_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
elseif(TSAN)
    set(SANITIZER_FLAGS "-fsanitize=thread")
endif()

if(SANITIZER_FLAGS)
    add_compile_options(${SANITIZER_FLAGS})
    add_link_options(${SANITIZER_FLAGS})
endif()

# Find ZLIB (IEEE CRC32)
find_package(ZLIB REQUIRED)

# Fetch third-party libraries
include(FetchContent)

# nlohmann_json
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

# GoogleTest
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Google FarmHash
FetchContent_Declare(
    farmhash
    URL https://github.com/google/farmhash/archive/refs/heads/master.tar.gz
)
FetchContent_GetProperties(farmhash)
if(NOT farmhash_POPULATED)
    FetchContent_Populate(farmhash)
    add_library(farmhash STATIC ${farmhash_SOURCE_DIR}/src/farmhash.cc)
    target_include_directories(farmhash PUBLIC ${farmhash_SOURCE_DIR}/src)
    target_compile_definitions(farmhash PRIVATE FARMHASH_NO_BUILTIN_EXPECT)
endif()

# Core library
set(MINI_LSM_SOURCES
    src/key.cpp
    src/block/builder.cpp
    src/block/iterator.cpp
    src/block_cache.cpp
    src/table/bloom.cpp
    src/table/builder.cpp
    src/table/iterator.cpp
    src/iterators/mem_table_iterator.cpp
    src/iterators/merge_iterator.cpp
    src/iterators/two_merge_iterator.cpp
    src/iterators/concat_iterator.cpp
    src/compact/simple_leveled.cpp
    src/compact/tiered.cpp
    src/compact/leveled.cpp
    src/mem_table.cpp
    src/wal.cpp
    src/manifest.cpp
    src/lsm_iterator.cpp
    src/lsm_storage.cpp
    src/compact.cpp
)

add_library(mini_lsm ${MINI_LSM_SOURCES})
target_include_directories(mini_lsm PUBLIC include)
target_link_libraries(mini_lsm PUBLIC ZLIB::ZLIB nlohmann_json::nlohmann_json farmhash pthread)

# CLI Binary
add_executable(mini_lsm_cli bin/mini_lsm_cli.cpp)
target_link_libraries(mini_lsm_cli PRIVATE mini_lsm)

# Tests
enable_testing()
include(GoogleTest)

function(add_mini_lsm_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE mini_lsm gtest gtest_main)
    gtest_discover_tests(${test_name})
endfunction()

add_mini_lsm_test(test_block tests/test_block.cpp)
add_mini_lsm_test(test_block_cache tests/test_block_cache.cpp)
add_mini_lsm_test(test_mem_table tests/test_mem_table.cpp)
add_mini_lsm_test(test_table tests/test_table.cpp)
add_mini_lsm_test(test_bloom tests/test_bloom.cpp)
add_mini_lsm_test(test_iterators tests/test_iterators.cpp)
add_mini_lsm_test(test_wal tests/test_wal.cpp)
add_mini_lsm_test(test_manifest tests/test_manifest.cpp)
add_mini_lsm_test(test_simple_leveled tests/test_simple_leveled.cpp)
add_mini_lsm_test(test_tiered tests/test_tiered.cpp)
add_mini_lsm_test(test_leveled tests/test_leveled.cpp)
add_mini_lsm_test(test_lsm_storage tests/test_lsm_storage.cpp)
add_mini_lsm_test(test_compact tests/test_compact.cpp)
add_mini_lsm_test(test_integration tests/test_integration.cpp)
