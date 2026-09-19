#include "temp_build_dir.hpp"

#include <catch2/catch_all.hpp>

#include <cstdlib>
#include <fstream>
#include <random>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#endif

namespace codegen_test_helpers {

    namespace fs = std::filesystem;

    TempBuildDir::TempBuildDir() {
        static thread_local std::mt19937 gen{std::random_device{}()};
        std::uniform_int_distribution<std::uint64_t> dist{};
        this->directory = fs::temp_directory_path() / ("sqlite2orm_build_" + std::to_string(dist(gen)));
        std::error_code ec;
        fs::create_directories(this->directory, ec);
        REQUIRE_FALSE(ec);
    }

    TempBuildDir::~TempBuildDir() {
        std::error_code ec;
        fs::remove_all(this->directory, ec);
    }

    fs::path TempBuildDir::file(std::string_view fileName) const {
        return this->directory / fs::path(fileName);
    }

    fs::path TempBuildDir::write(std::string_view fileName, std::string_view contents) const {
        const fs::path target = this->file(fileName);
        std::ofstream out(target);
        REQUIRE(out);
        out << contents;
        return target;
    }

    std::string TempBuildDir::compilerCommand() {
        std::string command = "c++ -std=c++20";
#if defined(__APPLE__)
        command += " -stdlib=libc++";
#endif
        command += " -I";
        command += SQLITE2ORM_TEST_SQLITE_ORM_INCLUDE;
        return command;
    }

    std::string TempBuildDir::sqlite3LinkFlags() {
        return SQLITE2ORM_TEST_SQLITE3_LINK;
    }

    int TempBuildDir::run(const std::string& command) {
        const int rawStatus = std::system(command.c_str());
#if defined(__unix__) || defined(__APPLE__)
        if(rawStatus != -1) {
            return WEXITSTATUS(rawStatus);
        }
#endif
        return rawStatus;
    }

}  // namespace codegen_test_helpers
