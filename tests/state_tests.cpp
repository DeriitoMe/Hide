#include "../src/input_state.hpp"
#include <cstdio>
#include <cstdlib>
using namespace typing_cursor;
static int checks = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #x);                            \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
int main() {
    Modifiers m;
    m.update(0xA2, true);
    m.update(0xA3, true);
    m.update(0xA2, false);
    CHECK(m.control == 2);
    m.update(0xA3, false);
    CHECK(m.control == 0);
    m.update(0xA4, true);
    m.update(0xA5, true);
    m.update(0xA4, false);
    CHECK(m.menu == 2);
    m.update(0xA5, false);
    CHECK(m.menu == 0);
    m.update(0x5B, true);
    m.update(0x5C, true);
    m.update(0x5B, false);
    CHECK(m.windows == 2);
    CHECK(!m.update('A', true));
    CHECK(classify_key('A', false, false, false, false) == KeyAction::Text);
    CHECK(classify_key('7', false, false, false, false) == KeyAction::Text);
    CHECK(classify_key(0x20, false, false, false, false) == KeyAction::Text);
    CHECK(classify_key(0xBA, false, false, false, false) == KeyAction::Text);
    CHECK(classify_key('V', true, false, false, false) == KeyAction::Text);
    CHECK(classify_key('C', true, false, false, false) == KeyAction::Ignore);
    CHECK(classify_key('S', true, false, false, false) == KeyAction::Ignore);
    CHECK(classify_key('A', false, true, false, false) == KeyAction::Ignore);
    CHECK(classify_key('A', false, false, true, false) == KeyAction::Ignore);
    CHECK(classify_key('Q', true, true, false, true) == KeyAction::Text);
    CHECK(classify_key(0x70, false, false, false, false) == KeyAction::Ignore);
    CHECK(classify_key(0x10, false, false, false, false) == KeyAction::Ignore);
    CHECK(classify_key(0xE5, false, false, false, false) == KeyAction::Text);
    CHECK(classify_key(0xE7, false, false, false, false) == KeyAction::Text);
    CHECK(classify_key(0x08, false, false, false, false) == KeyAction::Text);
    CHECK(classify_key(0x08, true, false, false, false) == KeyAction::Text);
    CHECK(classify_key(0x08, false, true, false, false) == KeyAction::Ignore);
    CHECK(classify_key(0x08, false, false, true, false) == KeyAction::Ignore);
    CHECK(classify_key(0x2E, false, false, false, false) == KeyAction::ContinueEditing);
    InputState s;
    s.invalidate(1);
    s.resolve(Focus::Editable, 1, 100);
    s.text(101, 1);
    CHECK(s.faded);
    s.mouse(2);
    CHECK(!s.faded && !s.pending);
    s.text(102, 3);
    CHECK(s.faded);
    s.tick(1601);
    CHECK(s.faded);
    s.tick(1602);
    CHECK(!s.faded);
    s.invalidate(2);
    s.text(2000, 4);
    CHECK(s.pending && !s.faded);
    s.resolve(Focus::Editable, 1, 2001);
    CHECK(s.pending && !s.faded);
    s.resolve(Focus::Editable, 2, 2002);
    CHECK(s.faded && !s.pending);
    s.invalidate(3);
    s.text(3000, 5);
    s.mouse(6);
    s.resolve(Focus::Editable, 3, 3001);
    CHECK(!s.faded);
    s.text(3002, 5);
    CHECK(!s.faded); // delayed typing message cannot undo a newer mouse action
    s.text(3003, 7);
    CHECK(s.faded);
    s.pause(true);
    CHECK(!s.faded && !s.enabled);
    s.text(3004, 8);
    CHECK(!s.faded);
    s.pause(false);
    s.text(3005, 9);
    CHECK(s.faded);
    s.invalidate(4);
    s.resolve(Focus::ReadOnly, 4, 4000);
    s.text(4001, 10);
    CHECK(!s.faded && !s.pending);
    s.invalidate(5);
    s.text(5000, 11);
    s.resolve(Focus::Editable, 5, 5401);
    CHECK(!s.faded);
    s.text(5500, 12);
    CHECK(s.faded);
    s.resolve(Focus::ReadOnly, 5, 5501);
    CHECK(!s.faded);
    s.invalidate(6);
    s.resolve(Focus::Editable, 6, 6000);
    auto backspace = classify_key(0x08, false, false, false, false);
    s.text(6000, 12, backspace == KeyAction::ContinueEditing);
    CHECK(s.faded); // Deletion can start fading independently of earlier typing.
    s.mouse(13);
    s.text(6001, 13, true);
    CHECK(!s.faded);
    s.text(6002, 14);
    s.text(6100, 15, true);
    s.tick(7599);
    CHECK(s.faded);
    s.tick(7600);
    CHECK(!s.faded);
    s.text(8000, 16);
    s.idle_ms = 0;
    s.tick(999999);
    CHECK(s.faded);
    s.mouse(17);
    CHECK(!s.faded);
    InputState deletion;
    deletion.invalidate(1);
    deletion.resolve(Focus::Editable, 1, 0);
    for (uint64_t i = 1; i <= 50; ++i) {
        deletion.text(i * 100, i, backspace == KeyAction::ContinueEditing);
        deletion.tick(i * 100 + 99);
        CHECK(deletion.faded);
    }
    deletion.tick(6499);
    CHECK(deletion.faded);
    deletion.tick(6500);
    CHECK(!deletion.faded);
    deletion.invalidate(2);
    deletion.text(7000, 51, backspace == KeyAction::ContinueEditing);
    CHECK(deletion.pending);
    deletion.mouse(52);
    deletion.resolve(Focus::Editable, 2, 7001);
    CHECK(!deletion.faded && !deletion.pending);
    deletion.resolve(Focus::ReadOnly, 2, 7002);
    deletion.text(7003, 53, backspace == KeyAction::ContinueEditing);
    CHECK(!deletion.faded && !deletion.pending);
    for (unsigned i = 0; i < 10000; ++i) {
        s.text(1000000 + i, 18 + i * 2);
        CHECK(s.faded);
        s.mouse(19 + i * 2);
        CHECK(!s.faded);
    }
    std::printf(
        "PASS: %d assertions, keyboard policy, focus races, restoration, 10,000 transitions.\n",
        checks);
}
