"""Read-only source validation and isolated cursor resource test copies.

Does not install cursors, change settings, or capture input.
"""
from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

SOURCE = Path(r"C:\WINDOWS\Cursors\Ikaros")
OUTPUT = Path(__file__).parent


def require(condition, message):
    if not condition:
        raise ValueError(message)


def chunks(data, start, end):
    pos = start
    while pos < end:
        require(end - pos >= 8, f"truncated chunk header at {pos}")
        tag, size = struct.unpack_from("<4sI", data, pos)
        body = pos + 8
        stop = body + size
        require(stop <= end, f"chunk {tag!r} exceeds boundary")
        next_pos = stop + (size & 1)
        require(next_pos <= end, f"missing padding for {tag!r}")
        yield tag, data[body:stop], pos
        pos = next_pos
    require(pos == end, "chunk boundary mismatch")


def chunk(tag, body):
    return tag + struct.pack("<I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def decode_dib(raw, entry):
    require(len(raw) >= 40, "DIB header truncated")
    values = struct.unpack_from("<IiiHHIIiiII", raw)
    header, width, double_height, planes, bpp, compression, image_size, xppm, yppm, used, important = values
    require(header == 40, f"unsupported DIB header length {header}")
    require(width > 0 and double_height != 0 and abs(double_height) % 2 == 0, "invalid DIB dimensions")
    height = abs(double_height) // 2
    require((width, height) == (entry["width"], entry["height"]), "CUR/DIB dimension mismatch")
    require(planes == 1, "invalid planes")
    require(bpp in (1, 4, 8, 24, 32), f"unsupported bit depth {bpp}")
    require(compression == 0, f"unsupported DIB compression {compression}")
    require(bpp <= 8 or used == 0, "unexpected high-color palette")
    colors = (used or (1 << bpp)) if bpp <= 8 else 0
    require(colors <= (1 << bpp) if bpp <= 8 else colors == 0, "invalid palette count")
    palette_end = 40 + colors * 4
    xor_stride = ((width * bpp + 31) // 32) * 4
    and_stride = ((width + 31) // 32) * 4
    xor_size = xor_stride * height
    and_size = and_stride * height
    require(image_size in (0, xor_size, xor_size + and_size), f"unexpected biSizeImage {image_size}")
    require(len(raw) == palette_end + xor_size + and_size, "DIB pixel/mask length mismatch")
    palette = [tuple(raw[40 + n * 4:43 + n * 4]) for n in range(colors)]
    pixels = []
    masks = []
    invert = 0
    for y in range(height):
        row = y if double_height < 0 else height - 1 - y
        xb = palette_end + row * xor_stride
        ab = palette_end + xor_size + row * and_stride
        for x in range(width):
            mask = (raw[ab + x // 8] >> (7 - x % 8)) & 1
            if bpp == 8:
                index = raw[xb + x]
                require(index < colors, "palette index outside table")
                rgb = palette[index]
                alpha = 255 if mask == 0 else 0
            elif bpp == 4:
                index = (raw[xb + x // 2] >> (4 if x % 2 == 0 else 0)) & 15
                require(index < colors, "palette index outside table")
                rgb = palette[index]
                alpha = 255 if mask == 0 else 0
            elif bpp == 1:
                index = (raw[xb + x // 8] >> (7 - x % 8)) & 1
                require(index < colors, "palette index outside table")
                rgb = palette[index]
                alpha = 255 if mask == 0 else 0
            elif bpp == 24:
                rgb = tuple(raw[xb + x * 3:xb + x * 3 + 3])
                alpha = 255 if mask == 0 else 0
            else:
                *rgb, alpha = raw[xb + x * 4:xb + x * 4 + 4]
                rgb = tuple(rgb)
            if bpp != 32 and mask and any(rgb):
                invert += 1
            pixels.append((*rgb, alpha))
            masks.append(mask)
    native_alpha = bpp == 32 and any(p[3] for p in pixels)
    if bpp == 32 and not native_alpha:
        pixels = [(*p[:3], 0 if m else 255) for p, m in zip(pixels, masks)]
        invert = sum(1 for p, m in zip(pixels, masks) if m and any(p[:3]))
    partial = [p for p in pixels if 0 < p[3] < 255]
    alpha_values = sorted(set(p[3] for p in pixels))
    premultiplied_compatible = all(max(p[:3]) <= p[3] for p in partial)
    meta = dict(width=width, height=height, bpp=bpp, compression=compression,
                palette_entries=colors, dib_header_size=header, dib_bytes=len(raw),
                xor_bytes=xor_size, and_mask_bytes=and_size, bitmap_bottom_up=double_height > 0,
                native_alpha=native_alpha, alpha_values=alpha_values,
                partial_alpha_pixels=len(partial), opaque_pixels=sum(p[3] == 255 for p in pixels),
                transparent_pixels=sum(p[3] == 0 for p in pixels),
                mask_one_pixels=sum(masks), xor_invert_pixels=invert,
                premultiplied_compatible=premultiplied_compatible,
                conversion_eligible=invert == 0)
    return meta, pixels


def encode_dib(meta, pixels, opacity, premultiply):
    width, height = meta["width"], meta["height"]
    stride = ((width + 31) // 32) * 4
    xor = bytearray()
    mask = bytearray(stride * height)
    native_premult = meta["native_alpha"] and meta["premultiplied_compatible"]
    for row in range(height):
        y = height - 1 - row
        for x in range(width):
            b, g, r, a = pixels[y * width + x]
            out_a = round(a * opacity)
            if out_a == 0:
                channels = (0, 0, 0)
            elif premultiply:
                channels = tuple(round(c * opacity) if native_premult else round(c * out_a / 255) for c in (b, g, r))
            else:
                channels = tuple(min(255, round(c * 255 / a)) if native_premult else c for c in (b, g, r))
            xor.extend((*channels, out_a))
            if out_a == 0:
                mask[row * stride + x // 8] |= 1 << (7 - x % 8)
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, len(xor), 0, 0, 0, 0)
    return header + xor + mask


def transform_cur(raw, variant=None):
    require(len(raw) >= 6, "CUR header truncated")
    reserved, kind, count = struct.unpack_from("<HHH", raw)
    require(reserved == 0 and kind in (1, 2) and count > 0, "invalid CUR/ICO header")
    table_end = 6 + count * 16
    require(table_end <= len(raw), "CUR image table truncated")
    entries, images, metadata, ranges = [], [], [], []
    for i in range(count):
        w, h, colors, res, a, b, size, offset = struct.unpack_from("<BBBBHHII", raw, 6 + 16 * i)
        require(res == 0, "CUR reserved byte nonzero")
        width, height = w or 256, h or 256
        require(size > 0 and offset >= table_end and offset + size <= len(raw), "invalid CUR image range")
        require(all(offset + size <= start or offset >= end for start, end in ranges), "overlapping CUR image ranges")
        ranges.append((offset, offset + size))
        entry = dict(index=i, width=width, height=height, color_count_byte=colors,
                     hotspot_x=a if kind == 2 else None, hotspot_y=b if kind == 2 else None,
                     resource_kind="CUR" if kind == 2 else "ICO", source_image_bytes=size)
        if kind == 2:
            require(a < width and b < height, "hotspot outside image")
        image = raw[offset:offset + size]
        require(not image.startswith(b"\x89PNG"), "PNG image unsupported by this DIB converter")
        meta, pixels = decode_dib(image, entry)
        entry.update(meta)
        metadata.append(entry)
        if variant is not None:
            require(meta["conversion_eligible"], "XOR/invert pixels require background-sensitive rendering")
            opacity, premultiply = variant
            image = encode_dib(meta, pixels, opacity, premultiply)
            colors = 0
            if kind == 1:
                a, b = 1, 32
        images.append(image)
        entries.append([w, h, colors, res, a, b])
    if variant is None:
        return raw, metadata
    rebuilt = bytearray(struct.pack("<HHH", reserved, kind, count))
    offset = table_end
    for e, image in zip(entries, images):
        rebuilt.extend(struct.pack("<BBBBHHII", *e, len(image), offset))
        offset += len(image)
    for image in images:
        rebuilt.extend(image)
    return bytes(rebuilt), metadata


def transform_ani(raw, variant=None):
    require(len(raw) >= 12 and raw[:4] == b"RIFF" and raw[8:12] == b"ACON", "invalid RIFF ACON header")
    require(struct.unpack_from("<I", raw, 4)[0] + 8 == len(raw), "RIFF size differs from file size")
    records = list(chunks(raw, 12, len(raw)))
    headers = [body for tag, body, _ in records if tag == b"anih"]
    require(len(headers) == 1 and len(headers[0]) == 36, "missing/duplicate/malformed anih")
    values = struct.unpack("<9I", headers[0])
    cb, frame_count, step_count, cx, cy, bit_count, planes, jiffies, flags = values
    require(cb == 36 and frame_count > 0 and step_count > 0 and jiffies > 0, "invalid anih values")
    require(flags & 1 and not flags & ~3, "unsupported anih flags")
    rates = [body for tag, body, _ in records if tag == b"rate"]
    sequences = [body for tag, body, _ in records if tag == b"seq "]
    require(len(rates) <= 1 and len(sequences) <= 1, "duplicate rate/seq")
    require(not rates or len(rates[0]) == step_count * 4, "rate length mismatch")
    require(not sequences or len(sequences[0]) == step_count * 4, "seq length mismatch")
    require(bool(flags & 2) == bool(sequences), "seq flag/chunk mismatch")
    rate = list(struct.unpack(f"<{step_count}I", rates[0])) if rates else [jiffies] * step_count
    seq = list(struct.unpack(f"<{step_count}I", sequences[0])) if sequences else list(range(step_count))
    require(all(v > 0 for v in rate), "zero rate entry")
    require(all(v < frame_count for v in seq), "sequence frame outside range")
    frames = []
    rebuilt_chunks = []
    frame_lists = 0
    for tag, body, pos in records:
        if tag == b"LIST" and body[:4] == b"fram":
            frame_lists += 1
            require(len(body) >= 4, "LIST subtype missing")
            children = []
            for child_tag, child_body, _ in chunks(body, 4, len(body)):
                if child_tag == b"icon":
                    transformed, frame = transform_cur(child_body, variant)
                    frames.append(frame)
                    child_body = transformed
                children.append(chunk(child_tag, child_body))
            body = b"fram" + b"".join(children)
        rebuilt_chunks.append(chunk(tag, body))
    require(frame_lists == 1 and len(frames) == frame_count, "anih frame count differs from fram icons")
    body = b"ACON" + b"".join(rebuilt_chunks)
    output = b"RIFF" + struct.pack("<I", len(body)) + body if variant is not None else raw
    meta = dict(type="ANI", anih=dict(cb_size=cb, frames=frame_count, steps=step_count,
                cx=cx, cy=cy, bit_count=bit_count, planes=planes, default_jiffies=jiffies, flags=flags),
                rate_jiffies=rate, sequence=seq, cycle_ms=sum(rate) * 1000 / 60,
                frames=frames, decoded_bgra_bytes=sum(e["width"] * e["height"] * 4 for f in frames for e in f),
                source_dib_bytes=sum(e["dib_bytes"] for f in frames for e in f))
    return output, meta
