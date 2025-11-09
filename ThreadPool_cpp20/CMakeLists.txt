#cmake_minimum_required(VERSION 4.0)
#project(ThreadPool_cpp20)
#
#set(CMAKE_CXX_STANDARD 20)
#
#add_executable(ThreadPool_cpp20 main_test.cpp
#        ThreadPool.cpp
#        ThreadPool.h)

# C++20: 设置 CMake 最低版本
cmake_minimum_required(VERSION 3.16)

# 功能: 定义项目名称 (与你的构建日志匹配)
project(ThreadPool_cpp20 LANGUAGES CXX)

# C++20: 设置 C++ 标准为 20
set(CMAKE_CXX_STANDARD 20)
# C++20: 强制使用 C++20 标准，禁止编译器降级
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 功能:
# (新)
# 添加可执行文件 "ThreadPool_cpp20"，
# 源文件为 main.cpp 和 ThreadPool.cpp
# (注意：你的日志显示你在编译 main_test.cpp，
#  请确保你的 CMakeLists.txt
#  使用的是下面这行，指向 main.cpp)
add_executable(ThreadPool_cpp20 main_test.cpp ThreadPool.cpp)

# C++11:
# 链接 pthread 库 (在 Linux/macOS 上需要)
# 逻辑: C++ std::thread 在底层依赖 pthread
if(UNIX)
    # 功能: 链接 POSIX 线程库
    target_link_libraries(ThreadPool_cpp20 PRIVATE pthread)
endif()

# 功能: 打印可执行文件路径
message(STATUS "Executable created: ${CMAKE_BINARY_DIR}/ThreadPool_cpp20")
