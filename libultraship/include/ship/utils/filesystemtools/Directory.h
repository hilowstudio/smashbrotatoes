#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <system_error>

#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

#undef GetCurrentDirectory
#undef CreateDirectory

class Directory {
  public:
#ifndef PATH_HACK
    static std::string GetCurrentDirectory() {
        // Non-throwing: current_path() throws on failure; return "" instead so
        // callers on a bad cwd don't unwind out of boot into std::terminate.
        std::error_code ec;
        auto p = fs::current_path(ec);
        return ec ? std::string() : p.string();
    }
#endif

    static bool Exists(const fs::path& path) {
        // noexcept overload: exists(path) throws on non-ENOENT errors (e.g. a
        // present-but-no-media drive → ERROR_NOT_READY on Windows), which has
        // crashed boot. Treat any error as "absent".
        std::error_code ec;
        return fs::exists(path, ec);
    }

    // Stupid hack because of Windows.h
    static void MakeDirectory(const std::string& path) {
        CreateDirectory(path);
    }

    static void CreateDirectory(const std::string& path) {
        try {
            fs::create_directories(path);
        } catch (...) {}
    }

    static std::vector<std::string> ListFiles(const std::string& dir) {
        std::vector<std::string> lst;

        if (Exists(dir)) {
            // Both the recursive_directory_iterator ctor and its operator++
            // throw on a mid-scan failure; drive it with error_code overloads
            // so a transient filesystem error ends the scan instead of
            // terminating the process (this path runs at boot via FolderArchive).
            std::error_code ec;
            fs::recursive_directory_iterator it(dir, ec), end;
            for (; !ec && it != end; it.increment(ec)) {
                std::error_code fec;
                if (!it->is_directory(fec))
                    lst.push_back(it->path().generic_string());
            }
        }

        return lst;
    }
};
