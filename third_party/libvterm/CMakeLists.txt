# Vendored libvterm 0.3.3 (MIT). Upstream ships no CMake build and generates
# its encoding tables at build time; this tree carries the pre-generated
# outputs (src/encoding/*.inc, src/fullwidth.inc) so the same sources compile
# on every platform, including Windows where no system libvterm exists.
# Source: https://github.com/neovim/libvterm (tag v0.3.3).
add_library(jot_vterm STATIC
  src/encoding.c
  src/keyboard.c
  src/mouse.c
  src/parser.c
  src/pen.c
  src/screen.c
  src/state.c
  src/unicode.c
  src/vterm.c
)
target_include_directories(jot_vterm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(jot_vterm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(jot_vterm PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_compile_definitions(jot_vterm PRIVATE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
  # C99 idioms (implicit conversions etc.) trip MSVC's default warnings; the
  # upstream code is intentional. Same suppressions Neovim uses for vterm.
  target_compile_options(jot_vterm PRIVATE
    /wd4090 /wd4244 /wd4267 /wd4018 /wd4389
  )
endif()
