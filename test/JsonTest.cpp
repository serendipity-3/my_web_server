//
// Created by 15565 on 2025/7/4.
//

#include <iostream>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

int main() {

    json myJson;
    myJson["name"] = "John";
    myJson["age"] = 18;

    json data;
    data["filename"] = "file_12341.html";
    myJson["data"] = data;

    string str = myJson.dump();
    cout << str << endl;

    return 0;
}