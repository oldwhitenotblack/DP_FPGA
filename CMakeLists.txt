cmake_minimum_required(VERSION 3.10)

project(MyProject VERSION 1.0)

# 设置包含目录
include_directories(include)

# 添加库
add_library(graph src/graph/graph.cpp)
add_library(dp src/dp/dp.cpp)

# 设置 Boost 路径（可以改为环境变量 BOOST_ROOT）
#set(BOOST_ROOT "D:\\FPGA_bipartite\\src\\alg\\boost_1_86_0\\boost_1_86_0")
#find_package(Boost 1.86.0 REQUIRED COMPONENTS system filesystem)
#if(Boost_FOUND)
#    message(STATUS "Found Boost")
#    include_directories(${Boost_INCLUDE_DIRS})
#    add_definitions(-DBOOST_ALL_NO_LIB)
#else()
#    message(FATAL_ERROR "Could not find Boost libraries")
#endif()

include_directories("D:\\FPGA_bipartite\\src\\alg\\boost_1_86_0\\boost_1_86_0")
link_directories("D:\\FPGA_bipartite\\src\\alg\\boost_1_86_0\\boost_1_86_0\\stage\\lib")
link_libraries("D:\\FPGA_bipartite\\src\\alg\\boost_1_86_0\\boost_1_86_0\\stage\\lib")
# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 指定源文件
set(SOURCES
        src/FileArchManage.cpp
        src/FileNetManage.cpp
        src/Test.cpp
        src/pack/Packble.cpp
        src/pack/ble.cpp
        src/pack/geo.h
        src/Database.cpp
        src/Database.h
        src/pack/matching.cpp
        src/pack/matching.h
        src/alg/bipartite.cpp
        src/alg/bipartite.h
        src/output.cpp
        src/output.h
        main.cpp
)

# 生成可执行文件并链接库
add_executable(MyProject ${SOURCES})
