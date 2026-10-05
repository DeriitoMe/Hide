#pragma once
#include <cstdint>

namespace typing_cursor {
enum class Focus { Unknown, Editable, ReadOnly };
enum class KeyAction { Ignore, Text, ContinueEditing };

struct Modifiers {
    unsigned control = 0, menu = 0, windows = 0;
    bool update(unsigned key, bool down) {
        unsigned *group = nullptr;
        unsigned bit = 0;
        switch (key) {
        case 0x11:
        case 0xA2:
            group = &control;
            bit = 1;
            break;
        case 0xA3:
            group = &control;
            bit = 2;
            break;
        case 0x12:
        case 0xA4:
            group = &menu;
            bit = 1;
            break;
        case 0xA5:
            group = &menu;
            bit = 2;
            break;
        case 0x5B:
            group = &windows;
            bit = 1;
            break;
        case 0x5C:
            group = &windows;
            bit = 2;
            break;
        default:
            return false;
        }
        if (down)
            *group |= bit;
        else
            *group &= ~bit;
        return true;
    }
};

inline KeyAction classify_key(unsigned key, bool ctrl, bool alt, bool win, bool right_alt) {
    if (win || (alt && !right_alt))
        return KeyAction::Ignore;
    const bool altgr = ctrl && right_alt;
    if (ctrl && !altgr)
        return key == 'V' || key == 0x08 ? KeyAction::Text : KeyAction::Ignore;
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || (key >= 0x60 && key <= 0x6F) ||
        (key >= 0xBA && key <= 0xC0) || (key >= 0xDB && key <= 0xDF) || key == 0xE2 ||
        key == 0xE5 || key == 0xE7 || key == 0x20 || key == 0x0D || key == 0x08)
        return KeyAction::Text;
    if (key == 0x2E || (key >= 0x21 && key <= 0x28))
        return KeyAction::ContinueEditing;
    return KeyAction::Ignore;
}

// Pure policy. UI, hooks and registry operations never run inside this class.
struct InputState {
    Focus focus = Focus::Unknown;
    bool enabled = true, faded = false, pending = false;
    uint64_t last_text = 0, text_sequence = 0, mouse_sequence = 0, generation = 0;
    uint64_t idle_ms = 1500;
    void invalidate(uint64_t next_generation) {
        generation = next_generation;
        focus = Focus::Unknown;
        faded = pending = false;
    }
    void text(uint64_t now, uint64_t sequence, bool continue_only = false) {
        if (!enabled || sequence <= mouse_sequence || (continue_only && !faded && !pending))
            return;
        last_text = now;
        text_sequence = sequence;
        if (focus == Focus::Editable) {
            faded = true;
            pending = false;
        } else if (focus == Focus::Unknown)
            pending = true;
    }
    void mouse(uint64_t sequence) {
        if (sequence > mouse_sequence)
            mouse_sequence = sequence;
        if (sequence > text_sequence)
            faded = pending = false;
    }
    void resolve(Focus value, uint64_t query_generation, uint64_t now) {
        if (query_generation != generation)
            return;
        focus = value;
        if (value != Focus::Editable)
            faded = false;
        if (pending) {
            faded = enabled && value == Focus::Editable && text_sequence > mouse_sequence &&
                    now >= last_text && now - last_text <= 400;
            pending = false;
        }
    }
    void tick(uint64_t now) {
        if ((faded || pending) && idle_ms && now >= last_text && now - last_text >= idle_ms)
            faded = pending = false;
    }
    void pause(bool paused) {
        enabled = !paused;
        if (paused)
            faded = pending = false;
    }
};
} // namespace typing_cursor
