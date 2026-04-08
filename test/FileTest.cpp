//
// Created by 15565 on 2025/7/4.
//

#include "FileProcess.h"

int main() {

    std::string name = generate_filename_by_time("file", "html");
    ok(LOG_TYPE::CONSOLE, name, __FILE__, __LINE__);

    return 0;
}