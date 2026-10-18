#include "still.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <fstream>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace pixwall {

namespace {

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

// %LOCALAPPDATA%\pixwall\desktop。OS の壁紙に入れる PNG と、入れる前の壁紙の覚え書きを置く
std::wstring DesktopDir() {
    PWSTR dir = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &dir))) return {};
    std::wstring path = std::wstring(dir) + L"\\pixwall";
    CoTaskMemFree(dir);
    CreateDirectoryW(path.c_str(), nullptr);
    path += L"\\desktop";
    CreateDirectoryW(path.c_str(), nullptr);
    return path;
}

bool IsOurs(const std::wstring& path) {
    const std::wstring dir = DesktopDir() + L"\\";
    return path.size() > dir.size() && CompareStringOrdinal(path.c_str(), static_cast<int>(dir.size()), dir.c_str(),
                                                            static_cast<int>(dir.size()), TRUE) == CSTR_EQUAL;
}

std::wstring Stem(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos ? name : name.substr(0, dot);
}

std::wstring Size(SIZE s) { return std::to_wstring(s.cx) + L"x" + std::to_wstring(s.cy); }

// 今つながっていない画面も含めて、どれかの画面の壁紙になっているパス
std::vector<std::wstring> WallpapersInUse(IDesktopWallpaper* desktop) {
    std::vector<std::wstring> paths;
    UINT count = 0;
    desktop->GetMonitorDevicePathCount(&count);
    for (UINT i = 0; i < count; ++i) {
        PWSTR id = nullptr;
        if (FAILED(desktop->GetMonitorDevicePathAt(i, &id))) continue;
        PWSTR current = nullptr;
        if (SUCCEEDED(desktop->GetWallpaper(id, &current)) && current) paths.push_back(current);
        CoTaskMemFree(current);
        CoTaskMemFree(id);
    }
    return paths;
}

// 作った PNG のうち、keep にも、どの画面の壁紙にもなっていないものを消す
// （つながっていない画面の壁紙が消えると、つなぎ直したときに壁紙が無くなるので）
void RemovePngs(IDesktopWallpaper* desktop, const std::vector<std::wstring>& keep) {
    std::vector<std::wstring> used = WallpapersInUse(desktop);
    used.insert(used.end(), keep.begin(), keep.end());
    const std::wstring dir = DesktopDir();
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring path = dir + L"\\" + fd.cFileName;
        bool inUse = false;
        for (const auto& u : used) inUse = inUse || CompareStringOrdinal(u.c_str(), -1, path.c_str(), -1, TRUE) == CSTR_EQUAL;
        if (!inUse) DeleteFileW(path.c_str());
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

}  // namespace

bool WritePng(IWICImagingFactory* wic, const std::wstring& path, const std::vector<uint8_t>& bgra, int width,
              int height) {
    if (!wic || bgra.size() != static_cast<size_t>(width) * height * 4) return false;
    const std::wstring temp = path + L".tmp";
    {
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(temp.c_str(), GENERIC_WRITE)) ||
            FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
            FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
            FAILED(frame->SetSize(width, height))) {
            DeleteFileW(temp.c_str());
            return false;
        }
        // 不透明なので、透明度の無い 24bit で書く（小さくなる）
        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        std::vector<uint8_t> bgr;
        bool ok = SUCCEEDED(frame->SetPixelFormat(&format));
        if (ok && format == GUID_WICPixelFormat24bppBGR) {
            bgr.resize(static_cast<size_t>(width) * height * 3);
            for (size_t i = 0, j = 0; i < bgra.size(); i += 4, j += 3) {
                bgr[j] = bgra[i];
                bgr[j + 1] = bgra[i + 1];
                bgr[j + 2] = bgra[i + 2];
            }
            ok = SUCCEEDED(frame->WritePixels(height, width * 3, static_cast<UINT>(bgr.size()), bgr.data()));
        } else {
            ok = false;
        }
        ok = ok && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
        if (!ok) {
            frame.Reset();
            encoder.Reset();
            stream.Reset();
            DeleteFileW(temp.c_str());
            return false;
        }
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

std::vector<SIZE> MonitorSizes() {
    std::vector<SIZE> sizes;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFO mi{sizeof(mi)};
            if (!GetMonitorInfoW(mon, &mi)) return TRUE;
            auto& list = *reinterpret_cast<std::vector<SIZE>*>(lp);
            const SIZE s{mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top};
            if (mi.dwFlags & MONITORINFOF_PRIMARY) {
                list.insert(list.begin(), s);
            } else {
                list.push_back(s);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&sizes));
    return sizes;
}

bool ExportStill(IWICImagingFactory* wic, const LoadedImage& image, const std::wstring& imagePath, size_t frame,
                 Fit fit, double position, uint8_t r, uint8_t g, uint8_t b, const std::wstring& path) {
    if (image.frames.empty() || path.empty()) return false;
    const auto& pixels = image.frames[frame < image.frames.size() ? frame : 0];
    const std::vector<SIZE> sizes = MonitorSizes();
    const std::wstring dir = path.substr(0, path.find_last_of(L"\\/") + 1);
    bool ok = !sizes.empty();
    for (size_t i = 0; i < sizes.size(); ++i) {
        std::wstring out = path;
        if (i > 0) {
            const std::wstring base = dir + Stem(imagePath) + L"-" + Size(sizes[i]);
            out = base + L".png";
            for (int n = 2; GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES; ++n) {
                out = base + L"-" + std::to_wstring(n) + L".png";
            }
        }
        const auto still =
            RenderStill(pixels.data(), image.width, image.height, sizes[i].cx, sizes[i].cy, fit, r, g, b, position);
        ok = WritePng(wic, out, still, sizes[i].cx, sizes[i].cy) && ok;
    }
    return ok;
}

void OsWallpaper::LoadOriginals() {
    loaded_ = true;
    originals_.clear();
    originalPosition_ = -1;
    std::ifstream in(DesktopDir() + L"\\original.txt", std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string key = line.substr(0, tab);
        const std::string value = line.substr(tab + 1);
        if (key == "position") {
            originalPosition_ = std::atoi(value.c_str());
        } else {
            originals_[Wide(key)] = Wide(value);
        }
    }
}

void OsWallpaper::SaveOriginals() const {
    const std::wstring path = DesktopDir() + L"\\original.txt";
    if (originals_.empty()) {
        DeleteFileW(path.c_str());
        return;
    }
    // 1行に「画面の ID<TAB>入れる前の壁紙のパス」。position の行は壁紙の置き方
    std::ostringstream text;
    text << "position\t" << originalPosition_ << "\n";
    for (const auto& [id, wallpaper] : originals_) text << Utf8(id) << "\t" << Utf8(wallpaper) << "\n";
    const std::wstring temp = path + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << text.str();
        if (!out) return;
    }
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void OsWallpaper::Apply(IWICImagingFactory* wic, const LoadedImage& image, const std::wstring& imagePath, Fit fit,
                        double imagePosition, uint8_t r, uint8_t g, uint8_t b) {
    if (image.frames.empty()) return;
    ComPtr<IDesktopWallpaper> desktop;
    if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktop)))) return;
    if (!loaded_) LoadOriginals();

    struct Monitor {
        std::wstring id;
        SIZE size;
        std::wstring current;
    };
    std::vector<Monitor> monitors;
    UINT count = 0;
    desktop->GetMonitorDevicePathCount(&count);
    std::wstring mark = imagePath + L"|" + std::to_wstring(static_cast<int>(fit)) + L"|" + std::to_wstring(imagePosition) +
                        L"|" + std::to_wstring(r) + L"," +
                        std::to_wstring(g) + L"," + std::to_wstring(b);
    for (UINT i = 0; i < count; ++i) {
        PWSTR id = nullptr;
        if (FAILED(desktop->GetMonitorDevicePathAt(i, &id))) continue;
        RECT rc{};
        // 今つながっていない画面は GetMonitorRECT が失敗する
        if (SUCCEEDED(desktop->GetMonitorRECT(id, &rc))) {
            PWSTR current = nullptr;
            desktop->GetWallpaper(id, &current);
            monitors.push_back({id, {rc.right - rc.left, rc.bottom - rc.top}, current ? current : L""});
            CoTaskMemFree(current);
            mark += L"|" + std::wstring(id) + L"=" + Size(monitors.back().size);
        }
        CoTaskMemFree(id);
    }
    if (monitors.empty()) return;
    // 自分の PNG が入ったままなら、前に作った印と同じ中身。本人が壁紙を替えていれば入れ直す
    bool allOurs = true;
    bool noneOurs = true;
    for (const Monitor& m : monitors) {
        allOurs = allOurs && IsOurs(m.current);
        noneOurs = noneOurs && !IsOurs(m.current);
    }
    if (mark == applied_ && allOurs) return;

    // 入れる前の壁紙を覚える。自分の PNG が入っている画面は、前に覚えた値のまま（落ちて戻せなかったときなど）
    for (const Monitor& m : monitors) {
        if (!IsOurs(m.current)) originals_[m.id] = m.current;
    }
    // 置き方は全画面で1つ。どの画面にも自分の PNG が無いときの値が、本人の置き方。
    // ただし中央は自分が替えたあとのことがある（つながっていない画面に自分の PNG が残ったまま、別の画面に替えたとき）
    // ので、前に覚えた値があればそちらを使う
    DESKTOP_WALLPAPER_POSITION position = DWPOS_FILL;
    if (noneOurs && SUCCEEDED(desktop->GetPosition(&position)) &&
        (position != DWPOS_CENTER || originalPosition_ < 0)) {
        originalPosition_ = position;
    }
    if (originalPosition_ < 0) originalPosition_ = DWPOS_FILL;
    SaveOriginals();

    // OS に拡大させるとドットがぼやけるので、画面の大きさちょうどに描いて、拡大しない置き方で入れる
    desktop->SetPosition(DWPOS_CENTER);
    const std::wstring dir = DesktopDir();
    // 同じ名前だと Explorer が前の絵を使い回すことがあるので、毎回名前を変える
    const std::wstring stamp = std::to_wstring(GetTickCount64());
    std::vector<std::wstring> keep;
    for (size_t i = 0; i < monitors.size(); ++i) {
        const Monitor& m = monitors[i];
        const std::wstring path = dir + L"\\" + std::to_wstring(i) + L"-" + Size(m.size) + L"-" + stamp + L".png";
        const auto still =
            RenderStill(image.frames[0].data(), image.width, image.height, m.size.cx, m.size.cy, fit, r, g, b,
                        imagePosition);
        if (WritePng(wic, path, still, m.size.cx, m.size.cy) && SUCCEEDED(desktop->SetWallpaper(m.id.c_str(), path.c_str()))) {
            keep.push_back(path);
        }
    }
    applied_ = mark;
    RemovePngs(desktop.Get(), keep);
}

void OsWallpaper::Restore() {
    if (!loaded_) LoadOriginals();
    applied_.clear();
    if (originals_.empty()) return;
    ComPtr<IDesktopWallpaper> desktop;
    if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktop)))) return;
    bool restored = false;
    // 戻せなかった画面（今つながっていないなど）は覚えたままにして、次に終えたときに戻す
    std::map<std::wstring, std::wstring> remaining;
    for (const auto& [id, wallpaper] : originals_) {
        PWSTR current = nullptr;
        if (FAILED(desktop->GetWallpaper(id.c_str(), &current))) {
            remaining[id] = wallpaper;
            continue;
        }
        const bool ours = current && IsOurs(current);
        CoTaskMemFree(current);
        // 本人がその間に替えた画面はそのまま
        if (!ours) continue;
        if (SUCCEEDED(desktop->SetWallpaper(id.c_str(), wallpaper.c_str()))) {
            restored = true;
        } else {
            remaining[id] = wallpaper;
        }
    }
    // 置き方は、自分が中央に替えたままなら戻す（その間に本人が壁紙だけ替えたときも、置き方は元のものにする）
    DESKTOP_WALLPAPER_POSITION position = DWPOS_FILL;
    if (originalPosition_ >= 0 && (restored || (SUCCEEDED(desktop->GetPosition(&position)) && position == DWPOS_CENTER))) {
        desktop->SetPosition(static_cast<DESKTOP_WALLPAPER_POSITION>(originalPosition_));
    }
    originals_ = std::move(remaining);
    SaveOriginals();
    // 戻した画面の PNG は要らない。まだ壁紙になっているもの（戻せなかった画面）は残す
    RemovePngs(desktop.Get(), {});
}

}  // namespace pixwall
