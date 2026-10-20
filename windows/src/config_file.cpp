#include "config_file.h"

#include <shlobj.h>

#include <fstream>

namespace pixwall {

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), static_cast<int>(w.size()));
    return w;
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    std::string s(WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr),
                  '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), static_cast<int>(s.size()),
                        nullptr, nullptr);
    return s;
}

// %APPDATA%\pixwall\config.json
std::wstring ConfigPath() {
    PWSTR dir = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &dir))) return {};
    std::wstring path = std::wstring(dir) + L"\\pixwall\\config.json";
    CoTaskMemFree(dir);
    return path;
}

Config LoadConfig(const std::wstring& path) {
    // 読んでいる間も、設定画面が書いたファイルで入れ替えられるよう、消す・名前を変えるも許して開く
    // （ifstream の開き方だと、その間の入れ替えがアクセス拒否になる）
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return ParseConfig({});
    std::string text;
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(file, buf, sizeof(buf), &read, nullptr) && read > 0) text.append(buf, read);
    CloseHandle(file);
    return ParseConfig(text);
}

bool SaveConfig(const std::wstring& path, const Config& config) {
    CreateDirectoryW(path.substr(0, path.find_last_of(L'\\')).c_str(), nullptr);
    // 書きかけで落ちても壊れないよう、別のファイルに書いてから入れ替える
    const std::wstring temp = path + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << FormatConfig(config);
        if (!out) return false;
    }
    // ほかのプロセス（ウイルス対策など）が開いていると入れ替えられないことがある。設定画面と同じく少し待って何回か試す
    for (int i = 0; i < 10; ++i) {
        if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        Sleep(10);
    }
    DeleteFileW(temp.c_str());
    return false;
}

}  // namespace pixwall
