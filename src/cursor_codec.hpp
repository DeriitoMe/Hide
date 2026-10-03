#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>

namespace hide_cursor {
using Bytes = std::vector<uint8_t>;
struct Metadata {
    unsigned frames = 1, steps = 1, images = 0, invert_pixels = 0;
    std::vector<uint32_t> rates, sequence;
    std::vector<uint16_t> hotspots;
};
// Input and decoded/output allocations are bounded independently. No system settings change here.
Bytes transform(const Bytes &source, unsigned transparency, Metadata *metadata = nullptr);
Bytes from_cursor(HCURSOR cursor, bool hidden = false);
Bytes read_file(const std::wstring &path);
void write_file(const std::wstring &path, const Bytes &bytes);
std::wstring digest(const Bytes &bytes);
} // namespace hide_cursor
