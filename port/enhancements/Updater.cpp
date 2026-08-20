#include "enhancements.h"

#include "port_log.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

namespace ssb64 {
namespace enhancements {

// We use atomics for the state flags so the UI thread doesn't have to lock a mutex 60 times a second!
static std::atomic<bool> s_updateChecked{false};
static std::atomic<bool> s_updateAvailable{false};
static std::atomic<bool> s_isDownloading{false};
static std::atomic<bool> s_downloadComplete{false};
static std::atomic<bool> s_isCheckingForUpdates{false};
static std::atomic<bool> s_updateCheckFailed{false};

static std::string s_latestVersion = "";
static std::string s_downloadUrl = "";
static std::string s_updateStatus = "";
static std::string s_downloadStatus = "";
static std::mutex s_stringMutex; // Only locks when reading/writing the actual text

namespace {

#ifndef SMASHBROTATOES_CURRENT_VERSION
#define SMASHBROTATOES_CURRENT_VERSION "v1.0.0"
#endif

constexpr const char* kLatestReleaseApi =
    "https://api.github.com/repos/hilowstudio/smashbrotatoes/releases/latest";
constexpr const char* kReleasePageUrl =
    "https://github.com/hilowstudio/smashbrotatoes/releases/latest";

constexpr const char* kPlatformAssetName = "SmashBrotatoes-windows.zip";

void SetUpdateStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(s_stringMutex);
    s_updateStatus = status;
}

void SetDownloadStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(s_stringMutex);
    s_downloadStatus = status;
}

// Run a child process with stdout/stderr redirected and no console window.
// Required on Windows because _popen() / system() shell through cmd.exe;
// when called from a /SUBSYSTEM:WINDOWS binary, that creates a visible console.
int RunCaptureNoWindow(std::string cmd,
                       const std::function<void(const std::string&)>& onLine) {
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return -1;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        return -1;
    }

    std::string line;
    char buf[512];
    DWORD got = 0;
    while (ReadFile(readPipe, buf, sizeof(buf), &got, nullptr) && got > 0) {
        for (DWORD i = 0; i < got; ++i) {
            char c = buf[i];
            if (c == '\r' || c == '\n') {
                if (!line.empty()) {
                    onLine(line);
                    line.clear();
                }
            } else {
                line += c;
            }
        }
    }
    if (!line.empty()) onLine(line);
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

int RunCapture(std::string cmd, const std::function<void(const std::string&)>& onLine) {
    return RunCaptureNoWindow(std::move(cmd), onLine);
}

std::string BuildCheckCommand() {
    return std::string("curl -fLsS -m 10 -H \"User-Agent: SmashBrotatoes-Updater\" ") +
           "\"" + kLatestReleaseApi + "\"";
}

std::string BuildDownloadCommand(const std::string& destPath, const std::string& url) {
    return "curl -fL -# -o \"" + destPath + "\" \"" + url + "\"";
}

// The URL is spliced into a curl command line, so reject anything that
// could escape the quoting or smuggle extra arguments. Release asset URLs
// are plain https://github.com/... paths — no quotes, spaces, or control
// characters.
bool IsSafeDownloadUrl(const std::string& url) {
    if (url.rfind("https://", 0) != 0) return false;
    for (char c : url) {
        if (c == '"' || c == '\'' || c == '\\' || c <= 0x20 || c == 0x7F) return false;
    }
    return true;
}

bool FindPlatformAssetUrl(const nlohmann::json& release, std::string* outUrl) {
    if (!release.contains("assets") || !release["assets"].is_array()) return false;

    for (const auto& asset : release["assets"]) {
        if (!asset.contains("name") || !asset.contains("browser_download_url")) continue;
        if (!asset["name"].is_string() || !asset["browser_download_url"].is_string()) continue;
        if (asset["name"].get<std::string>() == kPlatformAssetName) {
            std::string url = asset["browser_download_url"].get<std::string>();
            if (!IsSafeDownloadUrl(url)) {
                port_log("Updater: rejecting unsafe download URL\n");
                return false;
            }
            *outUrl = url;
            return true;
        }
    }
    return false;
}

void ResetDownloadStateForNewCheck() {
    s_downloadComplete.store(false);
    SetDownloadStatus("");
}

} // namespace

void CheckForUpdatesAsync(bool force) {
    // Check our atomic flags (no locks required)
    if (s_isCheckingForUpdates.load() || s_isDownloading.load()) return;
    if (!force && s_updateChecked.load()) return;

    s_updateChecked.store(true);
    s_isCheckingForUpdates.store(true);
    s_updateCheckFailed.store(false);
    s_updateAvailable.store(false);
    ResetDownloadStateForNewCheck();
    SetUpdateStatus("");

    std::thread([]() {
        std::string response;
        int exitCode = RunCapture(BuildCheckCommand(), [&](const std::string& line) {
            response += line;
        });
        if (exitCode != 0) {
            s_updateCheckFailed.store(true);
            SetUpdateStatus("Unable to check for updates.");
            s_isCheckingForUpdates.store(false);
            return;
        }

        if (!response.empty()) {
            try {
                auto release = nlohmann::json::parse(response);
                if (!release.contains("tag_name") || !release["tag_name"].is_string()) {
                    throw std::runtime_error("latest release missing tag_name");
                }

                std::string latestTag = release["tag_name"].get<std::string>();
                {
                    std::lock_guard<std::mutex> lock(s_stringMutex);
                    s_latestVersion = latestTag;
                    s_downloadUrl.clear();
                }

                if (latestTag != SMASHBROTATOES_CURRENT_VERSION) {
                    std::string assetUrl;
                    if (!FindPlatformAssetUrl(release, &assetUrl)) {
                        s_updateCheckFailed.store(true);
                        SetUpdateStatus("Latest release has no update for this platform.");
                    } else {
                        {
                            std::lock_guard<std::mutex> lock(s_stringMutex);
                            s_downloadUrl = assetUrl;
                        }
                        s_updateAvailable.store(true);
                        SetUpdateStatus("");
                    }
                } else {
                    SetUpdateStatus("Up to date.");
                }
            } catch (...) {
                s_updateCheckFailed.store(true);
                SetUpdateStatus("Unable to read update information.");
            }
        } else {
            s_updateCheckFailed.store(true);
            SetUpdateStatus("Unable to check for updates.");
        }

        s_isCheckingForUpdates.store(false);
    }).detach();
}

void StartGameUpdate() {
    if (s_isDownloading.load()) return;
    s_isDownloading.store(true);

    {
        std::lock_guard<std::mutex> lock(s_stringMutex);
        if (s_downloadUrl.empty()) {
            s_downloadStatus = "Error: No update download available.";
            s_isDownloading.store(false);
            return;
        }
        s_downloadStatus = "Initializing download...";
    }

    std::thread([]() {
        std::string url;
        {
            std::lock_guard<std::mutex> lock(s_stringMutex);
            url = s_downloadUrl;
        }

        // Anchor everything to the install directory — CWD can be anywhere
        // (Start Menu launches use the user profile), and the .bat extracts
        // over the game files, so it must run where SmashBrotatoes.exe lives.
        std::string exeDir;
        {
            char exePath[MAX_PATH];
            DWORD n = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
            if (n > 0 && n < MAX_PATH) {
                std::string p(exePath, n);
                size_t slash = p.find_last_of("\\/");
                if (slash != std::string::npos) {
                    exeDir = p.substr(0, slash);
                }
            }
        }
        if (exeDir.empty()) {
            {
                std::lock_guard<std::mutex> lock(s_stringMutex);
                s_downloadStatus = "Error: Could not locate install directory.";
            }
            s_isDownloading.store(false);
            return;
        }
        std::string tempZip = exeDir + "\\update_temp.zip";
        std::string cmd = BuildDownloadCommand(tempZip, url);

        auto onProgressLine = [](const std::string& line) {
            if (line.find('%') == std::string::npos) return;
            std::string pctStr;
            for (char ch : line) {
                if ((ch >= '0' && ch <= '9') || ch == '.') {
                    pctStr += ch;
                }
            }
            if (!pctStr.empty()) {
                SetDownloadStatus("Downloading... " + pctStr + "%");
            }
        };

        int exitCode = RunCapture(cmd, onProgressLine);

        if (exitCode == 0) {
            std::string batPath = exeDir + "\\update_game.bat";
            FILE* bat = fopen(batPath.c_str(), "w");
            if (bat) {
                fprintf(bat, "@echo off\n");
                fprintf(bat, "cd /d \"%s\"\n", exeDir.c_str());
                fprintf(bat, "echo Update downloaded! Waiting for SmashBrotatoes to close before applying...\n");
                fprintf(bat, ":wait\n");
                fprintf(bat, "tasklist /FI \"IMAGENAME eq SmashBrotatoes.exe\" 2>NUL | find /I /N \"SmashBrotatoes.exe\">NUL\n");
                fprintf(bat, "if \"%%ERRORLEVEL%%\"==\"0\" (\n");
                fprintf(bat, "    timeout /t 1 /nobreak > NUL\n");
                fprintf(bat, "    goto wait\n");
                fprintf(bat, ")\n");
                fprintf(bat, "echo Installing update...\n");
                fprintf(bat, "tar -xf update_temp.zip\n");
                fprintf(bat, "del update_temp.zip\n");
                fprintf(bat, "start SmashBrotatoes.exe\n");
                fprintf(bat, "(goto) 2>nul & del \"%%~f0\"\n");
                fclose(bat);

                {
                    std::lock_guard<std::mutex> lock(s_stringMutex);
                    s_downloadStatus = "Update ready! Close the game to apply.";
                }
                s_downloadComplete.store(true);
                ShellExecuteA(nullptr, "open", batPath.c_str(), nullptr, exeDir.c_str(), SW_SHOWNORMAL);
            } else {
                std::lock_guard<std::mutex> lock(s_stringMutex);
                s_downloadStatus = "Error: Failed to create updater script.";
            }
        } else {
            {
                std::lock_guard<std::mutex> lock(s_stringMutex);
                s_downloadStatus = "Error: Download failed.";
            }
            std::remove(tempZip.c_str());
        }

        s_isDownloading.store(false);
    }).detach();
}

// URL opener for the release page.
void OpenReleasePage() {
    const char* url = kReleasePageUrl;
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

// Atomic reads - no locks required for the UI thread!
bool IsCheckingForUpdates() { return s_isCheckingForUpdates.load(); }
bool IsUpdateAvailable() { return s_updateAvailable.load(); }
bool DidUpdateCheckFail() { return s_updateCheckFailed.load(); }
bool IsDownloading() { return s_isDownloading.load(); }
bool IsDownloadComplete() { return s_downloadComplete.load(); }

// String getters still need the lock, but they are incredibly fast now
std::string GetUpdateStatus() { std::lock_guard<std::mutex> lock(s_stringMutex); return s_updateStatus; }
std::string GetDownloadStatus() { std::lock_guard<std::mutex> lock(s_stringMutex); return s_downloadStatus; }
std::string GetLatestVersion() { std::lock_guard<std::mutex> lock(s_stringMutex); return s_latestVersion; }

} // namespace enhancements
} // namespace ssb64
