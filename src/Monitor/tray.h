#pragma once
#include <shellapi.h>
#include <strsafe.h>
#include <string>

class Tray {
    NOTIFYICONDATAW data{sizeof(data)};
    bool added = false;
    int lastPercent = -2;
    bool lastMemory = false;

    static HICON draw(int value, bool memory) {
        // Explorer scales this alpha bitmap to the notification area's current DPI.
        constexpr int size = 32;
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
        void* raw = nullptr;
        auto color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
        unsigned char clearMask[size * size / 8]{};
        auto mask = CreateBitmap(size, size, 1, 1, clearMask);
        HICON icon = nullptr;
        if (color && mask && raw) {
            auto pixels = static_cast<uint32_t*>(raw);
            const auto fill = memory ? 0xff66d5b1u : 0xff77bdfbu;
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    uint32_t pixel = 0;
                    if (x >= 2 && x <= 29 && y >= 3 && y <= 28) {
                        pixel = 0xff263647;
                        if (x >= 5 && x <= 26 && y >= 6 && y <= 25 && (x - 5) % 8 < 6 &&
                            y >= 26 - (20 * value + 50) / 100) pixel = fill;
                    }
                    pixels[y * size + x] = pixel;
                }
            }
            ICONINFO desc{TRUE, 0, 0, mask, color};
            icon = CreateIconIndirect(&desc);
        }
        if (color) DeleteObject(color);
        if (mask) DeleteObject(mask);
        return icon;
    }

public:
    void initialize(HWND window, UINT callback) {
        data.hWnd = window;
        data.uID = 1;
        data.uCallbackMessage = callback;
        data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    }

    void explorerRestarted() { added = false; }

    void update(const Snapshot& s, bool memory) {
        const int percent = memory ? s.memory : s.cpu;
        const auto percentText = [](int n) { return n < 0 ? std::wstring(L"—") : std::to_wstring(n) + L"%"; };
        const auto tip = L"Win Stats\nCPU " + percentText(s.cpu) + L"   Memory " + percentText(s.memory);
        const bool iconChanged = !added || percent != lastPercent || memory != lastMemory;
        if (!iconChanged && tip == data.szTip) return;
        StringCchCopyW(data.szTip, ARRAYSIZE(data.szTip), tip.c_str());
        data.uFlags = NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        if (iconChanged) {
            data.hIcon = draw(percent, memory);
            if (!data.hIcon) return;
            data.uFlags |= NIF_ICON;
        }
        if (Shell_NotifyIconW(added ? NIM_MODIFY : NIM_ADD, &data)) {
            if (!added) {
                data.uVersion = NOTIFYICON_VERSION_4;
                Shell_NotifyIconW(NIM_SETVERSION, &data);
            }
            added = true;
            lastPercent = percent;
            lastMemory = memory;
        } else {
            added = false;
        }
        if (data.hIcon) { DestroyIcon(data.hIcon); data.hIcon = nullptr; }
    }

    ~Tray() { if (added) Shell_NotifyIconW(NIM_DELETE, &data); }
};
