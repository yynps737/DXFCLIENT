#include "client.h"
#include <iostream>
#include <thread>
#include <csignal>

// 全局客户端对象，用于信号处理
DNFAutoClient* g_client = nullptr;

// 信号处理函数
void signalHandler(int signum) {
    std::cout << "收到信号: " << signum << std::endl;
    if (g_client) {
        std::cout << "正在停止客户端..." << std::endl;
        g_client->stop();
    }
    exit(signum);
}

int main(int argc, char* argv[]) {
    try {
        // 注册信号处理
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);

        // 创建客户端实例
        DNFAutoClient client;
        g_client = &client;

        // 初始化客户端
        if (!client.initialize()) {
            std::cerr << "客户端初始化失败" << std::endl;
            return -1;
        }

        std::cout << "客户端初始化成功，启动中..." << std::endl;

        // 启动客户端
        client.run();

        // 主循环
        std::cout << "客户端运行中，按 Ctrl+C 停止" << std::endl;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "未处理的异常: " << e.what() << std::endl;
        return -1;
    }
}