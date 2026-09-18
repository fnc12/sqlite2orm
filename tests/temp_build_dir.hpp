#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace codegen_test_helpers {

    /**
     *  A unique directory under the system temp dir that a test writes a small program into, and
     *  the compiler invocation around it. Generated code that compiles can still hand the caller a
     *  wrong value, so more than one probe builds — and runs — what the generator emitted; this
     *  holds the scaffolding they share. The directory goes away with the object.
     */
    class TempBuildDir {
      public:
        TempBuildDir();
        ~TempBuildDir();

        TempBuildDir(const TempBuildDir&) = delete;
        TempBuildDir& operator=(const TempBuildDir&) = delete;

        const std::filesystem::path& path() const {
            return this->directory;
        }
        /** Path of a file inside the directory, named `fileName`. */
        std::filesystem::path file(std::string_view fileName) const;
        /** Writes `contents` to `fileName` inside the directory and returns its path. */
        std::filesystem::path write(std::string_view fileName, std::string_view contents) const;

        /** `c++ -std=c++20`, the platform's stdlib flag and the sqlite_orm include the tests were configured with. */
        static std::string compilerCommand();
        /** Linker flags for the libsqlite3 CMake found, for a probe that runs the program it builds. */
        static std::string sqlite3LinkFlags();
        /** Runs `command` through the shell and returns its exit status. */
        static int run(const std::string& command);

      private:
        std::filesystem::path directory;
    };

}  // namespace codegen_test_helpers
