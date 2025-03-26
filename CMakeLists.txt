cmake_minimum_required(VERSION 3.15)
project(DNFAutomation)

# 设置vcpkg工具链
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加vcpkg安装路径
list(APPEND CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

# Windows特定设置
if(WIN32)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    # 避免重复定义NOMINMAX
    add_definitions(-DNOMINMAX)
    link_libraries(gdi32 user32 gdiplus ws2_32 wininet)
endif()

# 寻找依赖库
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)

# 源文件
set(SOURCES
        main.cpp
        base64.cpp
        client.cpp
        input_simulator.cpp
        screen_capture.cpp
        websocket_client.cpp
)

# 头文件
set(HEADERS
        base64.h
        client.h
        framework.h
        input_simulator.h
        Resource.h
        screen_capture.h
        targetver.h
        websocket_client.h
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 包含目录设置
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 链接库 - 使用静态库
target_link_libraries(${PROJECT_NAME} PRIVATE
        spdlog::spdlog_static
        fmt::fmt-static
        RapidJSON::rapidjson
        ixwebsocket::ixwebsocket
)

# 配置文件复制
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.ini
        ${CMAKE_CURRENT_BINARY_DIR}/config.ini COPYONLY)

# 图标文件复制
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/DNFAutoClient.ico
        ${CMAKE_CURRENT_BINARY_DIR}/DNFAutoClient.ico COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/small.ico
        ${CMAKE_CURRENT_BINARY_DIR}/small.ico COPYONLY)

# 创建日志目录
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/logs)