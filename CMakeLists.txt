cmake_minimum_required(VERSION 3.15)
project(lbzip2 C)

set(CMAKE_C_STANDARD 90)
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
