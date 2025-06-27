//
// Created by 15565 on 2025/6/27.
//

#include "ThreadPool.h"

int main(int argc, char *argv[]) {
    ThreadPool tp;
    for (int i = 0; i < 100; i++) {
        tp.submit([i]() {
            std::ostringstream os;
            os << "这是任务 " << i << " 号";
            ok(os.str(), LOG_TYPE::CONSOLE);
        });
    }

    tp.stop();
    ok("终于结束了", LOG_TYPE::CONSOLE);

    return 0;
}