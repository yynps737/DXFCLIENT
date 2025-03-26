#include "client.h"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        DNFAutoClient client;
        client.initialize();
        // 添加主程序逻辑
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return -1;
    }
}