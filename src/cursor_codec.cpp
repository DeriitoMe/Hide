#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "cursor_codec.hpp"
#include <wincodec.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <limits>

namespace hide_cursor {
constexpr size_t LIMIT = 32 * 1024 * 1024;
static void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static uint16_t u16(const Bytes &b, size_t p) {
    require(p <= b.size() && b.size() - p >= 2, "truncated cursor field");
    return uint16_t(b[p] | (unsigned(b[p + 1]) << 8));
}
static uint32_t u32(const Bytes &b, size_t p) {
    require(p <= b.size() && b.size() - p >= 4, "truncated cursor field");
    return uint32_t(b[p] | (unsigned(b[p + 1]) << 8) | (unsigned(b[p + 2]) << 16) |
                    (unsigned(b[p + 3]) << 24));
}
static void put16(Bytes &b, size_t p, unsigned v) {
    b.at(p) = uint8_t(v);
    b.at(p + 1) = uint8_t(v >> 8);
}
static void put32(Bytes &b, size_t p, uint32_t v) {
    for (unsigned n = 0; n < 4; ++n)
        b.at(p + n) = uint8_t(v >> (8 * n));
}
static Bytes slice(const Bytes &b, size_t p, size_t n) {
    require(p <= b.size() && n <= b.size() - p, "cursor range outside file");
    return Bytes(b.begin() + p, b.begin() + p + n);
}
static void append(Bytes &a, const Bytes &b) {
    require(b.size() <= LIMIT && a.size() <= LIMIT - b.size(),
            "cursor output exceeds 32 MiB limit");
    a.insert(a.end(), b.begin(), b.end());
}
template <class T> struct Interface {
    T *p = nullptr;
    ~Interface() {
        if (p)
            p->Release();
    }
    T **out() { return &p; }
};
struct Pixels {
    unsigned w = 0, h = 0;
    Bytes bgra, mask;
    bool native = false, premult = false;
};
static Pixels png(const Bytes &b, unsigned w, unsigned h) {
    Interface<IWICImagingFactory> factory;
    Interface<IWICStream> stream;
    Interface<IWICBitmapDecoder> decoder;
    Interface<IWICBitmapFrameDecode> frame;
    Interface<IWICFormatConverter> converter;
    require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(factory.out()))),
            "Windows image decoder unavailable");
    require(SUCCEEDED(factory.p->CreateStream(stream.out())) &&
                SUCCEEDED(stream.p->InitializeFromMemory(const_cast<BYTE *>(b.data()),
                                                         DWORD(b.size()))) &&
                SUCCEEDED(factory.p->CreateDecoderFromStream(
                    stream.p, nullptr, WICDecodeMetadataCacheOnDemand, decoder.out())) &&
                SUCCEEDED(decoder.p->GetFrame(0, frame.out())),
            "invalid PNG cursor");
    UINT actual_w = 0, actual_h = 0;
    require(SUCCEEDED(frame.p->GetSize(&actual_w, &actual_h)) && actual_w == w && actual_h == h,
            "PNG cursor dimensions differ from directory");
    require(SUCCEEDED(factory.p->CreateFormatConverter(converter.out())) &&
                SUCCEEDED(converter.p->Initialize(frame.p, GUID_WICPixelFormat32bppBGRA,
                                                  WICBitmapDitherTypeNone, nullptr, 0,
                                                  WICBitmapPaletteTypeCustom)),
            "PNG pixel conversion failed");
    Pixels p;
    p.w = w;
    p.h = h;
    p.native = true;
    p.bgra.resize(size_t(w) * h * 4);
    p.mask.resize(size_t(w) * h);
    require(SUCCEEDED(converter.p->CopyPixels(nullptr, w * 4, UINT(p.bgra.size()), p.bgra.data())),
            "PNG pixel decode failed");
    return p;
}
static unsigned channel(uint32_t pixel, uint32_t mask) {
    if (!mask)
        return 0;
    unsigned shift = 0;
    while (!(mask & 1)) {
        mask >>= 1;
        ++shift;
    }
    require((mask & (mask + 1)) == 0, "noncontiguous bitmap color mask");
    return unsigned(((uint64_t(pixel >> shift) & mask) * 255 + mask / 2) / mask);
}
static Pixels dib(const Bytes &b, unsigned w, unsigned h) {
    uint32_t header = u32(b, 0);
    require((header == 40 || header == 108 || header == 124) && header <= b.size(),
            "unsupported bitmap header");
    if (header == 124)
        require(u32(b, 112) == 0 && u32(b, 116) == 0, "embedded bitmap color profile unsupported");
    int32_t dw = int32_t(u32(b, 4)), dh = int32_t(u32(b, 8));
    require(dw == int32_t(w) && (dh == int32_t(h * 2) || dh == -int32_t(h * 2)),
            "bitmap dimensions differ from directory");
    unsigned bits = u16(b, 14);
    uint32_t compression = u32(b, 16), used = u32(b, 32);
    require(u16(b, 12) == 1 &&
                (bits == 1 || bits == 4 || bits == 8 || bits == 16 || bits == 24 || bits == 32),
            "unsupported bitmap depth");
    require(compression == 0 ||
                ((bits == 16 || bits == 32) && (compression == 3 || compression == 6)),
            "unsupported compressed bitmap");
    unsigned colors = bits <= 8 ? (used ? used : (1u << bits)) : 0;
    require(bits <= 8 ? colors <= (1u << bits) : used == 0, "invalid bitmap palette");
    size_t palette = header;
    uint32_t masks[4] = {bits == 16 ? 0x7c00u : 0xff0000u, bits == 16 ? 0x3e0u : 0xff00u,
                         bits == 16 ? 0x1fu : 0xffu, 0};
    if (compression == 3 || compression == 6) {
        for (unsigned i = 0; i < 3; ++i)
            masks[i] = u32(b, 40 + i * 4);
        if (header >= 108 || compression == 6)
            masks[3] = u32(b, 52);
        if (header == 40)
            palette += compression == 6 ? 16 : 12;
        require(masks[0] && masks[1] && masks[2] && !(masks[0] & masks[1]) &&
                    !(masks[0] & masks[2]) && !(masks[1] & masks[2]) &&
                    !(masks[3] & (masks[0] | masks[1] | masks[2])),
                "invalid bitmap color masks");
        if (bits == 16)
            require(((masks[0] | masks[1] | masks[2] | masks[3]) >> 16) == 0,
                    "bitmap masks exceed pixel depth");
    }
    size_t start = palette + colors * 4, stride = ((size_t(w) * bits + 31) / 32) * 4,
           ms = ((w + 31) / 32) * 4;
    require(start <= b.size() && stride * h <= b.size() - start, "truncated bitmap pixels");
    size_t mask_start = start + stride * h;
    bool have_mask = ms * h <= b.size() - mask_start;
    require(have_mask || bits == 32, "truncated bitmap AND mask");
    Pixels p;
    p.w = w;
    p.h = h;
    p.bgra.resize(size_t(w) * h * 4);
    p.mask.resize(size_t(w) * h);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            size_t row = dh > 0 ? h - 1 - y : y, pos = start + row * stride,
                   idx = size_t(y) * w + x;
            BYTE *out = p.bgra.data() + idx * 4;
            p.mask[idx] =
                have_mask ? uint8_t((b[mask_start + row * ms + x / 8] >> (7 - x % 8)) & 1) : 0;
            if (bits <= 8) {
                unsigned v = bits == 8   ? b[pos + x]
                             : bits == 4 ? ((b[pos + x / 2] >> (x % 2 ? 0 : 4)) & 15)
                                         : ((b[pos + x / 8] >> (7 - x % 8)) & 1);
                require(v < colors, "palette index outside table");
                memcpy(out, b.data() + palette + v * 4, 3);
            } else if (bits == 24)
                memcpy(out, b.data() + pos + x * 3, 3);
            else if (compression == 0 && bits == 32)
                memcpy(out, b.data() + pos + x * 4, 4);
            else {
                uint32_t v = bits == 16 ? u16(b, pos + x * 2) : u32(b, pos + x * 4);
                out[0] = uint8_t(channel(v, masks[2]));
                out[1] = uint8_t(channel(v, masks[1]));
                out[2] = uint8_t(channel(v, masks[0]));
                out[3] = uint8_t(channel(v, masks[3]));
            }
        }
    p.native = false;
    for (size_t n = 0; n < p.bgra.size(); n += 4) {
        unsigned a = p.bgra[n + 3];
        if ((bits == 32 || masks[3]) && a)
            p.native = true;
    }
    if (!p.native)
        for (size_t n = 0; n < p.mask.size(); ++n)
            p.bgra[n * 4 + 3] = p.mask[n] ? 0 : 255;
    return p;
}
static Bytes encode(const Pixels &p, unsigned transparency, Metadata &meta) {
    unsigned opacity = 100 - transparency, ms = ((p.w + 31) / 32) * 4;
    Bytes out(40 + size_t(p.w) * p.h * 4 + size_t(ms) * p.h);
    put32(out, 0, 40);
    put32(out, 4, p.w);
    put32(out, 8, p.h * 2);
    put16(out, 12, 1);
    put16(out, 14, 32);
    put32(out, 20, p.w * p.h * 4);
    for (unsigned y = 0; y < p.h; ++y)
        for (unsigned x = 0; x < p.w; ++x) {
            size_t n = (size_t(y) * p.w + x) * 4, pos = 40 + (size_t(p.h - 1 - y) * p.w + x) * 4;
            unsigned a = (p.bgra[n + 3] * opacity + 50) / 100;
            bool xor_pixel =
                !p.native && p.mask[n / 4] && (p.bgra[n] || p.bgra[n + 1] || p.bgra[n + 2]);
            if (xor_pixel) {
                ++meta.invert_pixels;
                require(opacity == 0 || (opacity <= 50 && p.bgra[n] == 255 &&
                                         p.bgra[n + 1] == 255 && p.bgra[n + 2] == 255),
                        "colored XOR cursor cannot preserve partial transparency");
                // (1-t)*background + t*(255-background) is source-over grey at alpha 2*t.
                a = (510 * opacity + 50) / 100;
                out[pos] = out[pos + 1] = out[pos + 2] = a ? 128 : 0;
            } else if (a)
                for (unsigned c = 0; c < 3; ++c)
                    out[pos + c] = p.bgra[n + c]; // CUR files use straight RGB; Windows
                                                  // premultiplies while loading.
            out[pos + 3] = uint8_t(a);
            if (!a)
                out[40 + size_t(p.w) * p.h * 4 + size_t(p.h - 1 - y) * ms + x / 8] |=
                    uint8_t(1 << (7 - x % 8));
        }
    return out;
}
static Bytes cur(const Bytes &b, unsigned transparency, Metadata &meta, size_t &budget) {
    require(u16(b, 0) == 0 && (u16(b, 2) == 1 || u16(b, 2) == 2), "invalid CUR/ICO header");
    unsigned count = u16(b, 4), kind = u16(b, 2);
    require(count && count <= 256 && 6 + size_t(count) * 16 <= b.size(),
            "invalid cursor image table");
    Bytes result = slice(b, 0, 6 + size_t(count) * 16);
    std::vector<std::pair<size_t, size_t>> ranges;
    for (unsigned i = 0; i < count; ++i) {
        size_t entry = 6 + i * 16;
        unsigned w = b[entry] ? b[entry] : 256, h = b[entry + 1] ? b[entry + 1] : 256;
        unsigned hx = u16(b, entry + 4), hy = u16(b, entry + 6);
        size_t size = u32(b, entry + 8), offset = u32(b, entry + 12);
        require(!b[entry + 3] && size && offset <= b.size() && size <= b.size() - offset,
                "invalid cursor image range");
        // The source table, rather than the growing output, bounds source offsets.
        require(offset >= 6 + size_t(count) * 16, "cursor image overlaps directory");
        for (auto range : ranges)
            require(offset + size <= range.first || offset >= range.second,
                    "overlapping cursor images");
        ranges.push_back({offset, offset + size});
        if (kind == 2)
            require(hx < w && hy < h, "cursor hotspot outside image");
        size_t charge = size_t(w) * h * 8;
        require(charge <= LIMIT - budget, "decoded cursor exceeds allocation limit");
        budget += charge;
        Bytes image = slice(b, offset, size);
        bool is_png = image.size() >= 8 && memcmp(image.data(), "\x89PNG\r\n\x1a\n", 8) == 0;
        Pixels pixels = is_png ? png(image, w, h) : dib(image, w, h);
        Bytes converted = encode(pixels, transparency, meta);
        result[entry + 2] = 0;
        if (kind == 1) {
            put16(result, entry + 4, 1);
            put16(result, entry + 6, 32);
        }
        put32(result, entry + 8, uint32_t(converted.size()));
        put32(result, entry + 12, uint32_t(result.size()));
        append(result, converted);
        ++meta.images;
        meta.hotspots.push_back(uint16_t(kind == 2 ? hx : w / 2));
        meta.hotspots.push_back(uint16_t(kind == 2 ? hy : h / 2));
    }
    return result;
}
struct Chunk {
    uint32_t tag;
    Bytes body;
};
static std::vector<Chunk> chunks(const Bytes &b, size_t start) {
    std::vector<Chunk> result;
    while (start < b.size()) {
        require(result.size() < 4096, "too many animation chunks");
        uint32_t tag = u32(b, start), n = u32(b, start + 4);
        start += 8;
        require(n <= b.size() - start && (n % 2 == 0 || n < b.size() - start),
                "animation chunk exceeds boundary");
        result.push_back({tag, slice(b, start, n)});
        start += n + (n & 1);
    }
    return result;
}
static uint32_t tag(const char *s) {
    return uint32_t(uint8_t(s[0])) | (uint32_t(uint8_t(s[1])) << 8) |
           (uint32_t(uint8_t(s[2])) << 16) | (uint32_t(uint8_t(s[3])) << 24);
}
static void chunk(Bytes &b, uint32_t t, const Bytes &body) {
    Bytes header(8);
    put32(header, 0, t);
    put32(header, 4, uint32_t(body.size()));
    append(b, header);
    append(b, body);
    if (body.size() & 1)
        append(b, Bytes(1));
}
Bytes transform(const Bytes &source, unsigned transparency, Metadata *metadata) {
    require(transparency == 50 || transparency == 75 || transparency == 90 || transparency == 100,
            "invalid transparency");
    require(source.size() >= 6 && source.size() <= LIMIT, "cursor file exceeds size limit");
    Metadata m;
    size_t budget = 0;
    Bytes result;
    if (source.size() >= 12 && u32(source, 0) == tag("RIFF")) {
        require(u32(source, 4) == source.size() - 8 && u32(source, 8) == tag("ACON"),
                "invalid ANI RIFF header");
        auto records = chunks(source, 12);
        const Bytes *header = nullptr, *rate = nullptr, *seq = nullptr;
        unsigned lists = 0, frames = 0;
        for (auto &c : records) {
            if (c.tag == tag("anih")) {
                require(!header, "duplicate animation header");
                header = &c.body;
            }
            if (c.tag == tag("rate")) {
                require(!rate, "duplicate animation rate");
                rate = &c.body;
            }
            if (c.tag == tag("seq ")) {
                require(!seq, "duplicate animation sequence");
                seq = &c.body;
            }
        }
        require(header && header->size() == 36 && u32(*header, 0) == 36,
                "invalid animation header");
        m.frames = u32(*header, 4);
        m.steps = u32(*header, 8);
        unsigned jiffies = u32(*header, 28), flags = u32(*header, 32);
        require(m.frames && m.frames <= 256 && m.steps && m.steps <= 4096 && jiffies &&
                    (flags & 1) && !(flags & ~3u),
                "unsupported animation header values");
        require(bool(flags & 2) == bool(seq) && (!rate || rate->size() == size_t(m.steps) * 4) &&
                    (!seq || seq->size() == size_t(m.steps) * 4),
                "animation rate or sequence mismatch");
        for (unsigned i = 0; i < m.steps; ++i) {
            unsigned r = rate ? u32(*rate, i * 4) : jiffies, s = seq ? u32(*seq, i * 4) : i;
            require(r && s < m.frames, "invalid animation timing or frame index");
            m.rates.push_back(r);
            m.sequence.push_back(s);
        }
        Bytes body(4);
        put32(body, 0, tag("ACON"));
        for (auto &c : records) {
            if (c.tag == tag("LIST") && c.body.size() >= 4 && u32(c.body, 0) == tag("fram")) {
                ++lists;
                Bytes rebuilt(4);
                put32(rebuilt, 0, tag("fram"));
                for (auto &child : chunks(c.body, 4)) {
                    if (child.tag == tag("icon")) {
                        child.body = cur(child.body, transparency, m, budget);
                        ++frames;
                    }
                    chunk(rebuilt, child.tag, child.body);
                }
                c.body = std::move(rebuilt);
            }
            chunk(body, c.tag, c.body);
        }
        require(lists == 1 && frames == m.frames, "animation frame count mismatch");
        result.resize(8);
        put32(result, 0, tag("RIFF"));
        put32(result, 4, uint32_t(body.size()));
        append(result, body);
    } else
        result = cur(source, transparency, m, budget);
    if (metadata)
        *metadata = std::move(m);
    return result;
}
Bytes read_file(const std::wstring &path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(f != INVALID_HANDLE_VALUE, "cursor source unavailable");
    LARGE_INTEGER n{};
    FILETIME before{}, after{};
    Bytes b;
    bool ok = GetFileSizeEx(f, &n) && n.QuadPart >= 6 && n.QuadPart <= LIMIT &&
              GetFileTime(f, nullptr, nullptr, &before);
    if (ok) {
        b.resize(size_t(n.QuadPart));
        DWORD got = 0;
        ok = ReadFile(f, b.data(), DWORD(b.size()), &got, nullptr) && got == b.size() &&
             GetFileTime(f, nullptr, nullptr, &after) && CompareFileTime(&before, &after) == 0;
    }
    CloseHandle(f);
    require(ok, "cursor source changed or exceeds limit");
    return b;
}
void write_file(const std::wstring &path, const Bytes &b) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    require(f != INVALID_HANDLE_VALUE, "cursor cache unavailable");
    DWORD n = 0;
    bool ok = WriteFile(f, b.data(), DWORD(b.size()), &n, nullptr) && n == b.size();
    CloseHandle(f);
    require(ok, "cursor cache write failed");
}
std::wstring digest(const Bytes &b) {
    BCRYPT_ALG_HANDLE a = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    BYTE out[32]{};
    bool ok = BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
              BCryptCreateHash(a, &h, nullptr, 0, nullptr, 0, 0) >= 0 &&
              BCryptHashData(h, const_cast<BYTE *>(b.data()), ULONG(b.size()), 0) >= 0 &&
              BCryptFinishHash(h, out, 32, 0) >= 0;
    if (h)
        BCryptDestroyHash(h);
    if (a)
        BCryptCloseAlgorithmProvider(a, 0);
    require(ok, "cursor digest failed");
    static const wchar_t hex[] = L"0123456789abcdef";
    std::wstring s;
    for (BYTE v : out) {
        s += hex[v >> 4];
        s += hex[v & 15];
    }
    return s;
}
Bytes from_cursor(HCURSOR c, bool hidden) {
    ICONINFOEXW extended{sizeof(extended)};
    if (GetIconInfoExW(c, &extended)) {
        if (extended.hbmColor)
            DeleteObject(extended.hbmColor);
        if (extended.hbmMask)
            DeleteObject(extended.hbmMask);
        HMODULE module = extended.szModName[0] ? GetModuleHandleW(extended.szModName) : nullptr;
        const wchar_t *name =
            extended.wResID ? MAKEINTRESOURCEW(extended.wResID) : extended.szResName;
        HRSRC resource = module ? FindResourceW(module, name, MAKEINTRESOURCEW(21)) : nullptr;
        if (resource) {
            DWORD size = SizeofResource(module, resource);
            auto bytes = static_cast<const BYTE *>(LockResource(LoadResource(module, resource)));
            require(bytes && size >= 12 && size <= LIMIT, "invalid system animated resource");
            return Bytes(bytes, bytes + size);
        }
    }
    ICONINFO i{};
    require(c && GetIconInfo(c, &i), "system default cursor unavailable");
    struct Cleanup {
        ICONINFO &i;
        ~Cleanup() {
            if (i.hbmColor)
                DeleteObject(i.hbmColor);
            if (i.hbmMask)
                DeleteObject(i.hbmMask);
        }
    } cleanup{i};
    BITMAP bm{};
    require(GetObjectW(i.hbmColor ? i.hbmColor : i.hbmMask, sizeof(bm), &bm) != 0,
            "system cursor bitmap unavailable");
    unsigned w = unsigned(bm.bmWidth), h = unsigned(i.hbmColor ? bm.bmHeight : bm.bmHeight / 2);
    require(w && h && w <= 256 && h <= 256 && i.xHotspot < w && i.yHotspot < h,
            "system cursor dimensions unsupported");
    if (!hidden) {
        // Never silently flatten a stock animation whose full source cannot be recovered.
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = 40;
        bi.bmiHeader.biWidth = LONG(w);
        bi.bmiHeader.biHeight = -LONG(h);
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void *pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
        HDC dc = CreateCompatibleDC(nullptr);
        if (!bitmap || !dc || !pixels) {
            if (dc)
                DeleteDC(dc);
            if (bitmap)
                DeleteObject(bitmap);
            throw std::runtime_error("default cursor animation check unavailable");
        }
        HGDIOBJ previous = SelectObject(dc, bitmap);
        Bytes first(size_t(w) * h * 4);
        bool animated = false;
        for (unsigned step = 0; step < 256; ++step) {
            memset(pixels, 0, first.size());
            if (!DrawIconEx(dc, 0, 0, c, int(w), int(h), step, nullptr, DI_NORMAL))
                break;
            GdiFlush();
            if (step == 0)
                memcpy(first.data(), pixels, first.size());
            else if (memcmp(first.data(), pixels, first.size()) != 0) {
                animated = true;
                break;
            }
        }
        SelectObject(dc, previous);
        DeleteDC(dc);
        DeleteObject(bitmap);
        require(!animated, "system animation has no recoverable source; partial fade unavailable");
    }
    unsigned ms = ((w + 31) / 32) * 4;
    Bytes image(40 + size_t(w) * h * 4 + size_t(ms) * h);
    put32(image, 0, 40);
    put32(image, 4, w);
    put32(image, 8, h * 2);
    put16(image, 12, 1);
    put16(image, 14, 32);
    put32(image, 20, w * h * 4);
    struct MaskInfo {
        BITMAPINFOHEADER h;
        RGBQUAD colors[2];
    } mi{};
    mi.h.biSize = 40;
    mi.h.biWidth = LONG(w);
    mi.h.biHeight = LONG(i.hbmColor ? h : h * 2);
    mi.h.biPlanes = 1;
    mi.h.biBitCount = 1;
    mi.colors[1] = {255, 255, 255, 0};
    Bytes masks(size_t(ms) * (i.hbmColor ? h : h * 2));
    HDC dc = GetDC(nullptr);
    bool ok = dc && GetDIBits(dc, i.hbmMask, 0, UINT(i.hbmColor ? h : h * 2), masks.data(),
                              reinterpret_cast<BITMAPINFO *>(&mi), DIB_RGB_COLORS) != 0;
    if (ok && i.hbmColor) {
        BITMAPINFO bi{};
        bi.bmiHeader = mi.h;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biHeight = LONG(h);
        ok = GetDIBits(dc, i.hbmColor, 0, h, image.data() + 40, &bi, DIB_RGB_COLORS) != 0;
        memcpy(image.data() + 40 + size_t(w) * h * 4, masks.data(), size_t(ms) * h);
        // GetIconInfo returns a premultiplied bitmap; serialize straight RGB for the file loader.
        for (size_t n = 40; n < 40 + size_t(w) * h * 4; n += 4) {
            unsigned a = image[n + 3];
            if (a > 0 && a < 255)
                for (unsigned color = 0; color < 3; ++color)
                    image[n + color] =
                        uint8_t(std::min(255u, (unsigned(image[n + color]) * 255 + a / 2) / a));
        }
    } else if (ok) {
        // Combined monochrome bitmap is bottom-up: XOR rows precede AND rows in memory.
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x) {
                BYTE v = ((masks[size_t(y) * ms + x / 8] >> (7 - x % 8)) & 1) ? 255 : 0;
                size_t p = 40 + (size_t(y) * w + x) * 4;
                image[p] = image[p + 1] = image[p + 2] = v;
            }
        memcpy(image.data() + 40 + size_t(w) * h * 4, masks.data() + size_t(ms) * h,
               size_t(ms) * h);
    }
    if (dc)
        ReleaseDC(nullptr, dc);
    require(ok, "system cursor bitmap read failed");
    Bytes b(22);
    put16(b, 2, 2);
    put16(b, 4, 1);
    b[6] = uint8_t(w);
    b[7] = uint8_t(h);
    put16(b, 10, i.xHotspot);
    put16(b, 12, i.yHotspot);
    put32(b, 14, uint32_t(image.size()));
    put32(b, 18, 22);
    append(b, image);
    return b;
}
} // namespace hide_cursor
