// GUI keysym translation (src/ui/gui/gui_input.cpp).
//
// SDL delivers a printable key twice: once as SDL_KEYDOWN and once as
// SDL_TEXTINPUT. jot forwards both to the editor as EVENT_KEY, so any printable
// character translated here as well arrives twice. Space was the one printable
// key mapped by the translator, which made a typed space occupy two cells -- at
// the default tab_size of 2 that reads exactly like a tab stop ("space is too
// far"), while letters stayed single-width.
//
// The terminal backend has no such duplication: termkey produces one key per
// keystroke, which is the convention the GUI has to match.
#include "ui/gui/gui.h"

#include <SDL2/SDL.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Printable keys are left to SDL text input", "[jot]")
{
  // The regression: Space must not be translated, or it lands twice.
  REQUIRE(translate_sdl_keysym(SDLK_SPACE) == 0);
  // Every other printable key already relied on this rule.
  REQUIRE(translate_sdl_keysym(SDLK_a) == 0);
  REQUIRE(translate_sdl_keysym(SDLK_z) == 0);
  REQUIRE(translate_sdl_keysym(SDLK_0) == 0);
  REQUIRE(translate_sdl_keysym(SDLK_MINUS) == 0);
  REQUIRE(translate_sdl_keysym(SDLK_SLASH) == 0);
}

TEST_CASE("Non-text keys keep their jot codes", "[jot]")
{
  // These never come through SDL_TEXTINPUT, so they must be translated here --
  // and the values must match what translate_termkey_keysym produces for the
  // terminal backend, since the editor's handlers are shared.
  REQUIRE(translate_sdl_keysym(SDLK_BACKSPACE) == 127);
  REQUIRE(translate_sdl_keysym(SDLK_TAB) == '\t');
  REQUIRE(translate_sdl_keysym(SDLK_RETURN) == 13);
  REQUIRE(translate_sdl_keysym(SDLK_KP_ENTER) == 13);
  REQUIRE(translate_sdl_keysym(SDLK_ESCAPE) == 27);
  REQUIRE(translate_sdl_keysym(SDLK_DELETE) == 1001);
  REQUIRE(translate_sdl_keysym(SDLK_UP) == 1008);
  REQUIRE(translate_sdl_keysym(SDLK_DOWN) == 1009);
  REQUIRE(translate_sdl_keysym(SDLK_HOME) == 1012);
  REQUIRE(translate_sdl_keysym(SDLK_END) == 1013);
  // Function keys keep their dedicated range.
  REQUIRE(translate_sdl_keysym(SDLK_F1) != 0);
  REQUIRE(translate_sdl_keysym(SDLK_F12) != 0);
}
