cmake_minimum_required(VERSION 3.22.1)

project(my_web_server)


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(OpenSSL REQUIRED)

set(SOURCES
        src/main.cpp
        src/HttpProcess.cpp
        src/ThreadPool.cpp
        src/MyLog.cpp
)

add_executable(MyLogTest test/MyLogTest.cpp src/MyLog.cpp)

target_include_directories(MyLogTest PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(WebServer ${SOURCES})

# 指定头文件目录（可选，如果需要 include/ 里的 test.h 等）
target_include_directories(WebServer PRIVATE ${CMAKE_SOURCE_DIR}/include)

# 链接 OpenSSL 库
target_link_libraries(WebServer PRIVATE OpenSSL::SSL OpenSSL::Crypto)
