//
// Created by 15565 on 2025/7/4.
//

#include "FileProcess.h"

int main() {

    std::string name = generate_filename_by_time("file", "html");
    ok(name, LOG_TYPE::CONSOLE);

    return 0;
}