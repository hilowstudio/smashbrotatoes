#include "app_paths.h"

#include <libultraship/libultraship.h>

#include <filesystem>
#include <system_error>

#include <windows.h>

namespace ssb64 {

std::string RealAppBundlePath() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (len != 0 && len < MAX_PATH) {
        std::filesystem::path exe(buf, buf + len);
        return exe.parent_path().string();
    }
    return Ship::Context::GetAppBundlePath();
}

} // namespace ssb64
