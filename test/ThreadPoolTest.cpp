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
            ok(LOG_TYPE::ALL, os.str(), __FILE__, __LINE__);
        });
    }

    tp.stop();
    ok(LOG_TYPE::ALL, "终于结束了", __FILE__, __LINE__);

    return 0;
}