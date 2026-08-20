#include "native_dialog.h"
#include "port_log.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlwapi.h>

namespace ssb64 {

std::string OpenFileDialog(const std::string& title,
                           const std::vector<std::string>& extensions) {
    // Build a Windows commdlg filter:  "Title\0*.z64;*.n64;*.v64\0\0".
    std::wstring filterDesc;
    for (wchar_t c : title) filterDesc.push_back(c);
    std::wstring filterPat;
    for (size_t i = 0; i < extensions.size(); ++i) {
        if (i) filterPat += L";";
        filterPat += L"*.";
        for (char c : extensions[i]) filterPat.push_back((wchar_t)c);
    }
    if (filterPat.empty()) filterPat = L"*.*";

    std::wstring filter = filterDesc + L'\0' + filterPat + L'\0' + L'\0';

    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    std::wstring titleW;
    for (char c : title) titleW.push_back((wchar_t)c);
    ofn.lpstrTitle = titleW.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) {
        return {};
    }
    // UTF-16 -> UTF-8 conversion for the returned path.
    int len = WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, nullptr, 0,
                                  nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, fileBuf, -1, out.data(), len,
                        nullptr, nullptr);
    return out;
}

} // namespace ssb64
