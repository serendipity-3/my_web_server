//
// HttpParseTest.cpp
// HTTP 解析单元测试
//

#include <gtest/gtest.h>
#include "HttpProcess.h"

// ========== check_http_request_status 测试 ==========

class HttpParseStatusTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// 测试：完整的 GET 请求（无 body）
TEST_F(HttpParseStatusTest, CompleteGetRequest) {
    std::string request = "GET /index.html HTTP/1.1\r\n"
                          "Host: localhost:8888\r\n"
                          "User-Agent: curl/7.81.0\r\n"
                          "Accept: */*\r\n"
                          "\r\n";

    EXPECT_EQ(check_http_request_status(request), HttpParseResult::Complete);
}

// 测试：完整的 POST 请求（有 body）
TEST_F(HttpParseStatusTest, CompletePostRequest) {
    std::string request = "POST /api/data HTTP/1.1\r\n"
                          "Host: localhost:8888\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: 27\r\n"
                          "\r\n"
                          "{\"name\":\"test\",\"value\":123}";

    EXPECT_EQ(check_http_request_status(request), HttpParseResult::Complete);
}

// 测试：不完整的请求（缺少 \r\n\r\n）
TEST_F(HttpParseStatusTest, IncompleteMissingHeaderEnd) {
    std::string request = "GET /index.html HTTP/1.1\r\n"
                          "Host: localhost:8888\r\n";

    EXPECT_EQ(check_http_request_status(request), HttpParseResult::Incomplete);
}

// 测试：不完整的 POST（body 不完整）
TEST_F(HttpParseStatusTest, IncompletePostBody) {
    std::string request = "POST /api/data HTTP/1.1\r\n"
                          "Host: localhost:8888\r\n"
                          "Content-Length: 100\r\n"
                          "\r\n"
                          "short body";

    EXPECT_EQ(check_http_request_status(request), HttpParseResult::Incomplete);
}

// 测试：Header 太大（超过 8KB）
TEST_F(HttpParseStatusTest, HeaderTooLarge) {
    std::string request = "GET / HTTP/1.1\r\n";
    // 添加大量 header 使其超过 8KB
    for (int i = 0; i < 500; i++) {
        request += "X-Custom-Header-" + std::to_string(i) + ": " + std::string(20, 'x') + "\r\n";
    }
    request += "\r\n";

    EXPECT_GT(request.size(), 8 * 1024);
    EXPECT_EQ(check_http_request_status(request), HttpParseResult::HeaderTooLarge);
}

// 测试：Body 太大（超过 4MB）
TEST_F(HttpParseStatusTest, BodyTooLarge) {
    std::string body_size = std::to_string(5 * 1024 * 1024); // 5MB
    std::string request = "POST /upload HTTP/1.1\r\n"
                          "Content-Length: " + body_size + "\r\n"
                          "\r\n";

    EXPECT_EQ(check_http_request_status(request), HttpParseResult::BodyTooLarge);
}

// 测试：Content-Length 解析失败（非数字）
TEST_F(HttpParseStatusTest, MalformedContentLength) {
    std::string request = "POST /api HTTP/1.1\r\n"
                          "Content-Length: abc\r\n"
                          "\r\n"
                          "body";

    EXPECT_EQ(check_http_request_status(request), HttpParseResult::MalformedHeader);
}

// 测试：空请求
TEST_F(HttpParseStatusTest, EmptyRequest) {
    std::string request = "";

    EXPECT_EQ(check_http_request_status(request), HttpParseResult::Incomplete);
}

// 测试：只有 header 结束标记的请求
TEST_F(HttpParseStatusTest, OnlyHeaderEnd) {
    std::string request = "\r\n\r\n";

    EXPECT_EQ(check_http_request_status(request), HttpParseResult::Complete);
}

// 测试：Content-Length 为 0 的 POST
TEST_F(HttpParseStatusTest, PostWithZeroContentLength) {
    std::string request = "POST /api HTTP/1.1\r\n"
                          "Content-Length: 0\r\n"
                          "\r\n";

    EXPECT_EQ(check_http_request_status(request), HttpParseResult::Complete);
}


// ========== request_str_to_map 测试 ==========

class RequestStrToMapTest : public ::testing::Test {
protected:
    std::map<std::string, std::string> request_map;

    void SetUp() override {
        request_map.clear();
    }
};

// 测试：基本 GET 请求解析
TEST_F(RequestStrToMapTest, BasicGetRequest) {
    std::string request = "GET /index.html HTTP/1.1\r\n"
                          "Host: localhost:8888\r\n"
                          "User-Agent: curl/7.81.0\r\n"
                          "\r\n";

    int result = request_str_to_map(request, request_map);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(request_map["Method"], "GET");
    EXPECT_EQ(request_map["Path"], "/index.html");
    EXPECT_EQ(request_map["Version"], "HTTP/1.1");
    EXPECT_EQ(request_map["Host"], "localhost:8888");
    EXPECT_EQ(request_map["User-Agent"], "curl/7.81.0");
}

// 测试：POST 请求带 body
TEST_F(RequestStrToMapTest, PostRequestWithBody) {
    std::string request = "POST /api/data HTTP/1.1\r\n"
                          "Host: localhost:8888\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: 27\r\n"
                          "\r\n"
                          "{\"name\":\"test\",\"value\":123}";

    int result = request_str_to_map(request, request_map);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(request_map["Method"], "POST");
    EXPECT_EQ(request_map["Path"], "/api/data");
    EXPECT_EQ(request_map["Content-Type"], "application/json");
    EXPECT_EQ(request_map["Content-Length"], "27");
    EXPECT_EQ(request_map["Body"], "{\"name\":\"test\",\"value\":123}");
}

// 测试：多个相同 header 的情况
TEST_F(RequestStrToMapTest, MultipleHeaders) {
    std::string request = "GET / HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "Accept: text/html\r\n"
                          "Accept-Language: en-US\r\n"
                          "Accept-Encoding: gzip, deflate\r\n"
                          "Connection: keep-alive\r\n"
                          "\r\n";

    int result = request_str_to_map(request, request_map);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(request_map.size(), 8);
}

// 测试：无效请求（无 \r\n\r\n）
TEST_F(RequestStrToMapTest, InvalidRequestNoHeaderEnd) {
    std::string request = "GET /index.html HTTP/1.1\r\n"
                          "Host: localhost:8888";

    int result = request_str_to_map(request, request_map);

    EXPECT_EQ(result, -1);
}

// 测试：header 值有前导空格
TEST_F(RequestStrToMapTest, HeaderValueWithLeadingSpace) {
    std::string request = "GET / HTTP/1.1\r\n"
                          "Host:   localhost\r\n"
                          "\r\n";

    int result = request_str_to_map(request, request_map);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(request_map["Host"], "localhost");
}

// 测试：空 body 的请求
TEST_F(RequestStrToMapTest, EmptyBody) {
    std::string request = "GET / HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "\r\n";

    int result = request_str_to_map(request, request_map);

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(request_map.find("Body") == request_map.end());
}

// 测试：DELETE 请求
TEST_F(RequestStrToMapTest, DeleteRequest) {
    std::string request = "DELETE /api/resource/123 HTTP/1.1\r\n"
                          "Host: localhost:8888\r\n"
                          "Authorization: Bearer token123\r\n"
                          "\r\n";

    int result = request_str_to_map(request, request_map);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(request_map["Method"], "DELETE");
    EXPECT_EQ(request_map["Path"], "/api/resource/123");
    EXPECT_EQ(request_map["Authorization"], "Bearer token123");
}

// 测试：OPTIONS 请求
TEST_F(RequestStrToMapTest, OptionsRequest) {
    std::string request = "OPTIONS * HTTP/1.1\r\n"
                          "Host: localhost:8888\r\n"
                          "\r\n";

    int result = request_str_to_map(request, request_map);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(request_map["Method"], "OPTIONS");
    EXPECT_EQ(request_map["Path"], "*");
}


// ========== 主函数 ==========

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
