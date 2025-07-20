cmake_minimum_required(VERSION 3.15)
project(lbzip2 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(
    _XOPEN_SOURCE=700
    _FILE_OFFSET_BITS=64
    PACKAGE_NAME="lbzip2"
    PACKAGE_VERSION="devel"
)
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/lib)

set(SRC_FILES
    src/compress.c
    src/crctab.c
    src/decode.c
    src/divbwt.c
    src/encode.c
    src/expand.c
    src/main.c
    src/parse.c
    src/process.c
    src/signals.c
    src/timespec.c
)

add_executable(lbzip2 ${SRC_FILES})
target_link_libraries(lbzip2 PRIVATE Threads::Threads)

enable_testing()
add_executable(driver tests/driver.c)
add_executable(minbzcat tests/minbzcat.c)

file(GLOB_RECURSE bz2_files_compress RELATIVE ${CMAKE_SOURCE_DIR}
        tests/suite/manual-compress/*.bz2
        tests/suite/fuzz-collect/*.bz2
        tests/suite/fuzz-divbwt/*.bz2)

file(GLOB_RECURSE bz2_files_expand RELATIVE ${CMAKE_SOURCE_DIR}
        tests/suite/manual-expand/*.bz2)

foreach(bz2_file ${bz2_files_compress})
    string(REGEX MATCH "tests/suite/([^/]+)/([^.]+)\\.bz2" _ "${bz2_file}")
    set(suite "${CMAKE_MATCH_1}")
    set(case_id "${CMAKE_MATCH_2}")
    string(REPLACE "-" "_" suite_safe "${suite}")
    set(test_name "${suite_safe}_${case_id}")
    add_test(NAME ${test_name}
            COMMAND driver compress ${CMAKE_SOURCE_DIR} ${suite} ${case_id})
endforeach()

foreach(bz2_file ${bz2_files_expand})
    string(REGEX MATCH "tests/suite/([^/]+)/([^.]+)\\.bz2" _ "${bz2_file}")
    set(suite "${CMAKE_MATCH_1}")
    set(case_id "${CMAKE_MATCH_2}")
    string(REPLACE "-" "_" suite_safe "${suite}")
    set(test_name "${suite_safe}_${case_id}")
    add_test(NAME ${test_name}
            COMMAND driver expand ${CMAKE_SOURCE_DIR} ${suite} ${case_id})
endforeach()
