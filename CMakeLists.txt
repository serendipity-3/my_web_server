cmake_minimum_required(VERSION 3.22.1)

project(my_web_server)


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#所有可执行文件都 include 这个
#include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(SOURCES
        src/main.cpp
        src/HttpProcess.cpp
        src/FileProcess.cpp
        src/ThreadPool.cpp
        src/MyLog.cpp
)

set(THREADPOOL
        test/ThreadPoolTest.cpp
        src/ThreadPool.cpp
        src/MyLog.cpp
)

add_executable(MyLogTest test/MyLogTest.cpp src/MyLog.cpp)
target_include_directories(MyLogTest PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(ThreadPoolTest ${THREADPOOL})
target_include_directories(ThreadPoolTest PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(FileTest test/FileTest.cpp src/FileProcess.cpp src/MyLog.cpp)
target_include_directories(FileTest PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(JsonTest test/JsonTest.cpp)

# 网络服务器
add_executable(WebServer ${SOURCES})
target_include_directories(WebServer PRIVATE ${CMAKE_SOURCE_DIR}/include)

# 链接各种库，可以使用 vcpkg 管理包
# 链接 OpenSSL 库
target_link_libraries(WebServer PRIVATE OpenSSL::SSL OpenSSL::Crypto)
# 连接 json 库
target_link_libraries(WebServer PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(JsonTest PRIVATE nlohmann_json::nlohmann_json)
