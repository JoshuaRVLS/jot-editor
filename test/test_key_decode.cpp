// Key event decoding (decode_key_event, shared by the terminal backend and
// the SDL2 GUI frontend): raw termkey-style codes carry modifier bits and
// the 0x8000 uppercase-letter bit in-band; the decoder strips them into the
// ctrl/shift/alt booleans the input layer dispatches on. These tests pin
// the contract both backends must produce -- most importantly that Ctrl+
// letter arrives as a clean control byte (1-26) so the shared +96 letter
// conversion in the event dispatcher works, and that high codepoints (CJK,
// nerd icons) are never mistaken for shifted letters.
#include "terminal.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("plain text keys decode clean", "[key-decode]")
{
  KeyEvent ev = decode_key_event('s');
  CHECK(ev.key == 's');
  CHECK_FALSE(ev.ctrl);
  CHECK_FALSE(ev.shift);
  CHECK_FALSE(ev.alt);
}

TEST_CASE("ctrl+letter is a control byte with ctrl set", "[key-decode]")
{
  // Raw form the terminal/GUI produce for Ctrl+S: control code 0x13,
  // optionally with the ctrl modifier bit (TERMKEY_FLAG_CTRLC).
  KeyEvent with_mod = decode_key_event(0x20013);
  CHECK(with_mod.key == 19);
  CHECK(with_mod.ctrl);
  CHECK_FALSE(with_mod.shift);

  KeyEvent bare = decode_key_event(19);
  CHECK(bare.key == 19);
  CHECK(bare.ctrl); // control-byte range implies ctrl
}

TEST_CASE("ctrl+shift+letter keeps shift", "[key-decode]")
{
  // Ctrl+Shift+S: control byte + shift modifier bit.
  KeyEvent ev = decode_key_event(0x80013);
  CHECK(ev.key == 19);
  CHECK(ev.ctrl);
  CHECK(ev.shift);
}

TEST_CASE("shifted letters lose the 0x8000 uppercase bit", "[key-decode]")
{
  // termkey marks shift+s as codepoint 'S' with the 0x8000 bit + shift flag.
  KeyEvent ev = decode_key_event(0x8000 | 'S' | 0x80000);
  CHECK(ev.key == 'S');
  CHECK(ev.shift);
  CHECK_FALSE(ev.ctrl);
}

TEST_CASE("alt+letter keeps the letter", "[key-decode]")
{
  KeyEvent ev = decode_key_event('s' | 0x40000);
  CHECK(ev.key == 's');
  CHECK(ev.alt);
  CHECK_FALSE(ev.ctrl);
}

TEST_CASE("control bytes for tab and enter do not imply ctrl", "[key-decode]")
{
  CHECK_FALSE(decode_key_event('\t').ctrl);
  CHECK_FALSE(decode_key_event(13).ctrl);
  // But ctrl+tab carries the explicit modifier bit.
  CHECK(decode_key_event('\t' | 0x20000).ctrl);
}

TEST_CASE("shifted arrows unwrap 2008-2011 to 1008-1011", "[key-decode]")
{
  KeyEvent up = decode_key_event(2008);
  CHECK(up.key == 1008); // Up
  CHECK(up.shift);

  KeyEvent down = decode_key_event(2011);
  CHECK(down.key == 1011); // Left
  CHECK(down.shift);
}

TEST_CASE("function keys keep their marker", "[key-decode]")
{
  int f5 = KeyCode::function(5);
  KeyEvent ev = decode_key_event(f5);
  CHECK(ev.key == f5);
  CHECK_FALSE(ev.ctrl);
  CHECK_FALSE(ev.shift);

  KeyEvent shifted = decode_key_event(f5 | 0x80000);
  CHECK(shifted.key == f5);
  CHECK(shifted.shift);
}

TEST_CASE("high codepoints are not mistaken for shifted letters", "[key-decode]")
{
  // Nerd icon (private use area) and CJK ideographs carry bit 15 naturally.
  KeyEvent icon = decode_key_event(0xE0B0);
  CHECK(icon.key == 0xE0B0);
  CHECK_FALSE(icon.shift);

  KeyEvent cjk = decode_key_event(0x8000); // U+8000 (CJK unified ideograph)
  CHECK(cjk.key == 0x8000);
  CHECK_FALSE(cjk.shift);

  KeyEvent cjk_shifted = decode_key_event(0x8000 | 0x80000);
  CHECK(cjk_shifted.key == 0x8000);
  CHECK(cjk_shifted.shift); // shift flag survives; the key value does not
}

TEST_CASE("shift+tab decodes to 1017 without shift", "[key-decode]")
{
  // Both backends emit the special 1017 code with the shift bit cleared.
  KeyEvent ev = decode_key_event(1017);
  CHECK(ev.key == 1017);
  CHECK_FALSE(ev.shift);
}

TEST_CASE("raw shifted arrow with the arrow code keeps the code", "[key-decode]")
{
  // The GUI emits shift+Up as 1008|shift instead of termkey's 2008; both
  // must decode to the same event.
  KeyEvent gui_up = decode_key_event(1008 | 0x80000);
  KeyEvent term_up = decode_key_event(2008);
  CHECK(gui_up.key == term_up.key);
  CHECK(gui_up.key == 1008);
  CHECK(gui_up.shift);
  CHECK(term_up.shift);
}