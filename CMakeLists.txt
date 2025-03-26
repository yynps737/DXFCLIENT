cmake_minimum_required(VERSION 3.15)
project(DNFAutomation)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows特定设置
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

# 源文件
set(SOURCES
        main.cpp
        base64.cpp
        client.cpp
        input_simulator.cpp
        screen_capture.cpp
        websocket_client.cpp
        LogWrapper.cpp
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
        LogWrapper.h
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 包含目录设置
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 链接Windows系统库
target_link_libraries(${PROJECT_NAME} PRIVATE
        gdi32 user32 gdiplus ws2_32 wininet wsock32 shlwapi crypt32 ole32 oleaut32 uuid comctl32
)

# 配置文件复制
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.ini
            ${CMAKE_CURRENT_BINARY_DIR}/config.ini COPYONLY)
else()
    # 创建默认配置文件
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/config.ini
            "[Server]
url=ws://localhost:8080
verify_ssl=false

[Capture]
interval=0.5
quality=80

[Game]
window_title=地下城与勇士

[Connection]
max_retries=5
retry_delay=5
heartbeat_interval=30
")
    message(STATUS "已创建默认配置文件 config.ini")
endif()

# 图标文件复制（如果存在）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/DNFAutoClient.ico")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/DNFAutoClient.ico
            ${CMAKE_CURRENT_BINARY_DIR}/DNFAutoClient.ico COPYONLY)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/small.ico")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/small.ico
            ${CMAKE_CURRENT_BINARY_DIR}/small.ico COPYONLY)
endif()

# 创建日志目录
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/logs)

# 输出构建信息
message(STATUS "C++标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "项目将构建在: ${CMAKE_CURRENT_BINARY_DIR}")