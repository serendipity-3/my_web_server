//
// FileProcessTest.cpp
// 文件处理单元测试
//

#include <gtest/gtest.h>
#include <fstream>
#include "FileProcess.h"

// ========== file_exists 测试 ==========

class FileExistsTest : public ::testing::Test {
protected:
    std::string test_file = "/tmp/gtest_file_exists_test.txt";

    void SetUp() override {
        std::ofstream ofs(test_file);
        ofs << "test content";
        ofs.close();
    }

    void TearDown() override {
        std::remove(test_file.c_str());
    }
};

TEST_F(FileExistsTest, ExistingFile) {
    EXPECT_TRUE(file_exists(test_file));
}

TEST_F(FileExistsTest, NonExistingFile) {
    std::string non_existing = "/tmp/gtest_non_existing_file_12345.txt";
    EXPECT_FALSE(file_exists(non_existing));
}


// ========== get_file_size 测试 ==========

class GetFileSizeTest : public ::testing::Test {
protected:
    std::string test_file = "/tmp/gtest_file_size_test.txt";
    std::string content = "Hello, World!";

    void SetUp() override {
        std::ofstream ofs(test_file);
        ofs << content;
        ofs.close();
    }

    void TearDown() override {
        std::remove(test_file.c_str());
    }
};

TEST_F(GetFileSizeTest, CorrectSize) {
    int64_t size = get_file_size(test_file);
    EXPECT_EQ(size, static_cast<int64_t>(content.size()));
}

TEST_F(GetFileSizeTest, NonExistingFile) {
    std::string non_existing = "/tmp/gtest_non_existing_size.txt";
    int64_t size = get_file_size(non_existing);
    EXPECT_EQ(size, -1);
}

TEST_F(GetFileSizeTest, EmptyFile) {
    std::string empty_file = "/tmp/gtest_empty_file.txt";
    std::ofstream ofs(empty_file);
    ofs.close();

    int64_t size = get_file_size(empty_file);
    EXPECT_EQ(size, 0);

    std::remove(empty_file.c_str());
}


// ========== get_file_content 测试 ==========

class GetFileContentTest : public ::testing::Test {
protected:
    std::string test_file = "/tmp/gtest_file_content_test.txt";

    void SetUp() override {}

    void TearDown() override {
        std::remove(test_file.c_str());
    }
};

TEST_F(GetFileContentTest, ReadTextFile) {
    std::string expected = "Hello, World!\nThis is a test file.\n";
    std::ofstream ofs(test_file);
    ofs << expected;
    ofs.close();

    std::string content = get_file_content(test_file);
    EXPECT_EQ(content, expected);
}

TEST_F(GetFileContentTest, ReadEmptyFile) {
    std::ofstream ofs(test_file);
    ofs.close();

    std::string content = get_file_content(test_file);
    EXPECT_TRUE(content.empty());
}

TEST_F(GetFileContentTest, ReadBinaryLikeContent) {
    std::string binary_content = "Line1\r\nLine2\r\nLine3";
    std::ofstream ofs(test_file);
    ofs << binary_content;
    ofs.close();

    std::string content = get_file_content(test_file);
    EXPECT_EQ(content, binary_content);
}


// ========== generate_filename_by_time 测试 ==========

class GenerateFilenameTest : public ::testing::Test {};

TEST_F(GenerateFilenameTest, GeneratesValidFilename) {
    std::string filename = generate_filename_by_time("file", "html");

    EXPECT_FALSE(filename.empty());
    EXPECT_TRUE(filename.find("file_") != std::string::npos);
    EXPECT_TRUE(filename.find(".html") != std::string::npos);
}

TEST_F(GenerateFilenameTest, DifferentCallsGiveDifferentNames) {
    std::string name1 = generate_filename_by_time("test", "txt");
    usleep(1100000); // 等待超过 1 秒确保时间戳不同
    std::string name2 = generate_filename_by_time("test", "txt");

    EXPECT_NE(name1, name2);
}

TEST_F(GenerateFilenameTest, DifferentPrefixAndPostfix) {
    std::string name1 = generate_filename_by_time("log", "txt");
    std::string name2 = generate_filename_by_time("data", "json");

    EXPECT_TRUE(name1.find("log_") != std::string::npos);
    EXPECT_TRUE(name1.find(".txt") != std::string::npos);
    EXPECT_TRUE(name2.find("data_") != std::string::npos);
    EXPECT_TRUE(name2.find(".json") != std::string::npos);
}


// ========== 主函数 ==========

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
