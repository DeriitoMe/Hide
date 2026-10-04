#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/cursor_codec.hpp"
#include "../src/scheme_tracker.hpp"
#include <objbase.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cstring>
using namespace hide_cursor;
static unsigned checks = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "FAILED %d: %s\n", __LINE__, #x);                                 \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
static void p16(Bytes &b, size_t p, unsigned v) {
    b.at(p) = uint8_t(v);
    b.at(p + 1) = uint8_t(v >> 8);
}
static void p32(Bytes &b, size_t p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        b.at(p + i) = uint8_t(v >> (8 * i));
}
static uint32_t g32(const Bytes &b, size_t p) {
    return uint32_t(b.at(p)) | (uint32_t(b.at(p + 1)) << 8) | (uint32_t(b.at(p + 2)) << 16) |
           (uint32_t(b.at(p + 3)) << 24);
}
static Bytes fixture(BYTE red, BYTE green, BYTE blue, BYTE alpha = 255) {
    Bytes b(22 + 40 + 8 * 8 * 4 + 4 * 8);
    p16(b, 2, 2);
    p16(b, 4, 1);
    b[6] = b[7] = 8;
    p16(b, 10, 3);
    p16(b, 12, 5);
    p32(b, 14, uint32_t(b.size() - 22));
    p32(b, 18, 22);
    p32(b, 22, 40);
    p32(b, 26, 8);
    p32(b, 30, 16);
    p16(b, 34, 1);
    p16(b, 36, 32);
    p32(b, 42, 256);
    for (size_t p = 62; p < 62 + 256; p += 4) {
        b[p] = blue;
        b[p + 1] = green;
        b[p + 2] = red;
        b[p + 3] = alpha;
    }
    return b;
}
static Bytes invert_fixture() {
    Bytes b(22 + 40 + 8 + 64);
    p16(b, 2, 2);
    p16(b, 4, 1);
    b[6] = b[7] = 8;
    p16(b, 10, 3);
    p16(b, 12, 5);
    p32(b, 14, uint32_t(b.size() - 22));
    p32(b, 18, 22);
    p32(b, 22, 40);
    p32(b, 26, 8);
    p32(b, 30, 16);
    p16(b, 34, 1);
    p16(b, 36, 1);
    b[66] = b[67] = b[68] = 255;
    for (size_t row = 0; row < 8; ++row) {
        b[70 + row * 4] = 255;
        b[102 + row * 4] = 255;
    }
    return b;
}
static void chunk(Bytes &b, const char *tag, const Bytes &body) {
    size_t p = b.size();
    b.resize(p + 8);
    memcpy(b.data() + p, tag, 4);
    p32(b, p + 4, uint32_t(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1)
        b.push_back(0);
}
static Bytes animated() {
    Bytes b(12);
    memcpy(b.data(), "RIFF", 4);
    memcpy(b.data() + 8, "ACON", 4);
    Bytes header(36);
    p32(header, 0, 36);
    p32(header, 4, 2);
    p32(header, 8, 3);
    p32(header, 28, 6);
    p32(header, 32, 3);
    chunk(b, "anih", header);
    Bytes rate(12), seq(12);
    p32(rate, 0, 4);
    p32(rate, 4, 8);
    p32(rate, 8, 6);
    p32(seq, 0, 1);
    p32(seq, 4, 0);
    p32(seq, 8, 1);
    chunk(b, "rate", rate);
    chunk(b, "seq ", seq);
    Bytes frames{'f', 'r', 'a', 'm'};
    chunk(frames, "icon", fixture(255, 0, 0));
    chunk(frames, "icon", fixture(0, 0, 255));
    chunk(b, "LIST", frames);
    p32(b, 4, uint32_t(b.size() - 8));
    return b;
}
static bool rejects(const Bytes &b) {
    try {
        transform(b, 75);
        return false;
    } catch (const std::exception &) {
        return true;
    }
}
static Bytes render(HCURSOR cursor, unsigned step, BYTE background, unsigned size = 8) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = 40;
    bi.bmiHeader.biWidth = LONG(size);
    bi.bmiHeader.biHeight = -LONG(size);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    CHECK(bitmap && bits);
    HDC dc = CreateCompatibleDC(nullptr);
    CHECK(dc);
    HGDIOBJ old = SelectObject(dc, bitmap);
    memset(bits, background, size_t(size) * size * 4);
    CHECK(DrawIconEx(dc, 0, 0, cursor, int(size), int(size), step, nullptr, DI_NORMAL) != 0);
    GdiFlush();
    Bytes out(static_cast<BYTE *>(bits), static_cast<BYTE *>(bits) + size_t(size) * size * 4);
    SelectObject(dc, old);
    DeleteDC(dc);
    DeleteObject(bitmap);
    return out;
}
static std::wstring temp_path(const wchar_t *name) {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    return std::wstring(temp) + L"Hide-test-" + std::to_wstring(GetCurrentProcessId()) + name;
}
static void rendering_tests() {
    auto animation_path = temp_path(L"-cannot-flatten.ani");
    write_file(animation_path, animated());
    HCURSOR animation = LoadCursorFromFileW(animation_path.c_str());
    CHECK(animation);
    bool rejected = false;
    try {
        from_cursor(animation);
    } catch (const std::exception &) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(!transform(from_cursor(animation, true), 100).empty());
    DestroyCursor(animation);
    DeleteFileW(animation_path.c_str());
    for (auto source : {fixture(255, 128, 64), fixture(64, 32, 16, 128), invert_fixture()})
        for (unsigned alpha : {50u, 75u, 90u, 100u}) {
            auto source_path = temp_path(L"-source.cur"), fade_path = temp_path(L"-fade.cur");
            write_file(source_path, source);
            write_file(fade_path, transform(source, alpha));
            HCURSOR original = HCURSOR(LoadImageW(nullptr, source_path.c_str(), IMAGE_CURSOR, 8, 8,
                                                  LR_LOADFROMFILE)),
                    fade = HCURSOR(LoadImageW(nullptr, fade_path.c_str(), IMAGE_CURSOR, 8, 8,
                                              LR_LOADFROMFILE));
            CHECK(original && fade);
            CHECK(cursor_appearance(original) != cursor_appearance(fade));
            for (BYTE bg : {BYTE(0), BYTE(255), BYTE(73)}) {
                Bytes a = render(original, 0, bg), b = render(fade, 0, bg);
                for (size_t i = 0; i < a.size(); ++i)
                    if (i % 4 != 3) {
                        int expected =
                            int((unsigned(a[i]) * (100 - alpha) + unsigned(bg) * alpha + 50) / 100);
                        if (std::abs(int(b[i]) - expected) > 2)
                            std::fprintf(stderr,
                                         "render source-bpp=%u mode=%u bg=%u pos=%zu original=%u "
                                         "actual=%u expected=%d\n",
                                         source.at(36), alpha, bg, i, a[i], b[i], expected);
                        CHECK(std::abs(int(b[i]) - expected) <= 2);
                    }
            }
            DestroyCursor(original);
            DestroyCursor(fade);
            DeleteFileW(source_path.c_str());
            DeleteFileW(fade_path.c_str());
        }
    // Validate the monochrome default extraction, including combined XOR/AND bitmap row ordering.
    auto stock_path = temp_path(L"-stock.cur");
    write_file(stock_path, invert_fixture());
    HCURSOR cursor =
        HCURSOR(LoadImageW(nullptr, stock_path.c_str(), IMAGE_CURSOR, 8, 8, LR_LOADFROMFILE));
    CHECK(cursor);
    Bytes converted = transform(from_cursor(cursor), 75);
    auto name = temp_path(L"-default.cur");
    write_file(name, converted);
    ICONINFO info{};
    CHECK(GetIconInfo(cursor, &info));
    BITMAP bm{};
    CHECK(GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bm), &bm));
    unsigned size = unsigned(bm.bmWidth);
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    HCURSOR fade = HCURSOR(
        LoadImageW(nullptr, name.c_str(), IMAGE_CURSOR, int(size), int(size), LR_LOADFROMFILE));
    CHECK(fade);
    for (BYTE bg : {BYTE(0), BYTE(255), BYTE(73)}) {
        Bytes a = render(cursor, 0, bg, size), b = render(fade, 0, bg, size);
        for (size_t i = 0; i < a.size(); ++i)
            if (i % 4 != 3) {
                int expected = int((unsigned(a[i]) * 25 + unsigned(bg) * 75 + 50) / 100);
                if (std::abs(int(b[i]) - expected) > 3)
                    std::fprintf(
                        stderr, "default bg=%u size=%u pos=%zu original=%u actual=%u expected=%d\n",
                        bg, size, i, a[i], b[i], expected);
                CHECK(std::abs(int(b[i]) - expected) <= 3);
            }
    }
    DestroyCursor(fade);
    DestroyCursor(cursor);
    DeleteFileW(name.c_str());
    DeleteFileW(stock_path.c_str());
}
static void desktop_tests() {
    auto path = temp_path(L"-animated.ani");
    Bytes b = transform(animated(), 75);
    write_file(path, b);
    HCURSOR c = LoadCursorFromFileW(path.c_str());
    CHECK(c);
    for (DWORD id : ids) {
        HCURSOR copy = LoadCursorFromFileW(path.c_str());
        CHECK(copy);
        BOOL ok = SetSystemCursor(copy, id);
        if (!ok)
            std::fprintf(stderr, "SetSystemCursor failed for %lu: %lu\n", id, GetLastError());
        CHECK(ok);
        HCURSOR live = LoadCursorW(nullptr, MAKEINTRESOURCEW(id));
        CHECK(live);
        for (unsigned step = 0; step < 3; ++step) {
            Bytes actual = render(live, step, 0), expected = render(c, step, 0);
            if (actual != expected) {
                restore_current_cursors();
            }
            CHECK(actual == expected);
        }
        CHECK(render(live, 0, 0) != render(live, 1, 0));
    }
    CHECK(restore_current_cursors());
    DestroyCursor(c);
    DeleteFileW(path.c_str());
}
int wmain(int argc, wchar_t **argv) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CHECK(SUCCEEDED(init));
    if (argc > 1 && std::wstring(argv[1]) == L"--scan") {
        unsigned passed = 0, failed = 0, cur_count = 0, ani_count = 0, invert_count = 0;
        for (auto &entry : std::filesystem::recursive_directory_iterator(L"C:\\Windows\\Cursors")) {
            auto extension = entry.path().extension().wstring();
            if (extension != L".cur" && extension != L".ani")
                continue;
            try {
                Metadata m;
                Bytes output = transform(read_file(entry.path().wstring()), 75, &m);
                Metadata second;
                transform(output, 75, &second);
                CHECK(m.frames == second.frames && m.steps == second.steps &&
                      m.sequence == second.sequence && m.rates == second.rates &&
                      m.hotspots == second.hotspots);
                auto file = temp_path(extension.c_str());
                write_file(file, output);
                HCURSOR c = LoadCursorFromFileW(file.c_str());
                CHECK(c);
                DestroyCursor(c);
                DeleteFileW(file.c_str());
                ++passed;
                if (extension == L".cur")
                    ++cur_count;
                else
                    ++ani_count;
                if (m.invert_pixels)
                    ++invert_count;
            } catch (const std::exception &e) {
                ++failed;
                std::printf("REJECT %ls: %s\n", entry.path().filename().c_str(), e.what());
            }
        }
        std::printf("{\"passed\":%u,\"rejected\":%u,\"cur\":%u,\"ani\":%u,\"invert_files\":%u}\n",
                    passed, failed, cur_count, ani_count, invert_count);
    } else {
        Metadata m;
        Bytes converted = transform(animated(), 75, &m);
        CHECK(m.frames == 2 && m.steps == 3 && m.images == 2);
        CHECK(m.rates == std::vector<uint32_t>({4, 8, 6}));
        CHECK(m.sequence == std::vector<uint32_t>({1, 0, 1}));
        CHECK(m.hotspots == std::vector<uint16_t>({3, 5, 3, 5}));
        Metadata repeated;
        transform(converted, 50, &repeated);
        CHECK(m.rates == repeated.rates && m.sequence == repeated.sequence &&
              m.hotspots == repeated.hotspots);
        CHECK(g32(converted, 4) == converted.size() - 8);
        Bytes png{137, 80,  78, 71,  13, 10,  26,  10, 0,   0,  0,   13,  73,  72,  68,  82,  0, 0,
                  0,   1,   0,  0,   0,  1,   8,   6,  0,   0,  0,   31,  21,  196, 137, 0,   0, 0,
                  13,  73,  68, 65,  84, 120, 156, 99, 216, 98, 115, 167, 1,   0,   5,   193, 2, 77,
                  202, 215, 67, 244, 0,  0,   0,   0,  73,  69, 78,  68,  174, 66,  96,  130};
        Bytes png_cur(22);
        p16(png_cur, 2, 2);
        p16(png_cur, 4, 1);
        png_cur[6] = png_cur[7] = 1;
        p32(png_cur, 14, uint32_t(png.size()));
        p32(png_cur, 18, 22);
        png_cur.insert(png_cur.end(), png.begin(), png.end());
        Bytes png_out = transform(png_cur, 75, &m);
        CHECK(m.images == 1);
        CHECK(png_out[62] == 220 && png_out[63] == 60 && png_out[64] == 180 && png_out[65] == 32);
        Bytes colored = invert_fixture();
        colored[66] = 0;
        CHECK(rejects(colored));
        CHECK(!transform(colored, 100).empty());
        Bytes multi = fixture(255, 0, 0), second = fixture(0, 0, 255), many(38);
        p16(many, 2, 2);
        p16(many, 4, 2);
        memcpy(many.data() + 6, multi.data() + 6, 16);
        memcpy(many.data() + 22, second.data() + 6, 16);
        p32(many, 18, 38);
        p32(many, 34, 38 + uint32_t(multi.size() - 22));
        many.insert(many.end(), multi.begin() + 22, multi.end());
        many.insert(many.end(), second.begin() + 22, second.end());
        transform(many, 75, &m);
        CHECK(m.images == 2);
        Bytes bad = many;
        p32(bad, 34, 38);
        CHECK(rejects(bad));
        bad = fixture(1, 2, 3);
        bad[10] = 8;
        CHECK(rejects(bad));
        bad = animated();
        p32(bad, 4, 1);
        CHECK(rejects(bad));
        bad = animated();
        p32(bad, 28, 257);
        CHECK(rejects(bad));
        bad = animated();
        p32(bad, 68, 0);
        CHECK(rejects(bad));
        Bytes source = animated();
        for (size_t size = 0; size < source.size(); ++size)
            CHECK(rejects(Bytes(source.begin(), source.begin() + size)));
        uint32_t random = 7;
        for (unsigned n = 0; n < 10000; ++n) {
            bad = source;
            random = random * 1664525 + 1013904223;
            size_t p = random % bad.size();
            random = random * 1664525 + 1013904223;
            bad[p] ^= uint8_t(1 << (random % 8));
            try {
                transform(bad, 75);
            } catch (const std::exception &) {
            }
            ++checks;
        }
        rendering_tests();
        if (argc > 1 && std::wstring(argv[1]) == L"--desktop")
            desktop_tests();
        std::printf("PASS: %u cursor parser, animation metadata, allocation boundary and rendering "
                    "assertions.\n",
                    checks);
    }
    CoUninitialize();
    return 0;
}
