# Vendored glad 2.0.8 (MIT / WTFPL OR CC0-1.0 AND Apache-2.0, see LICENSE).
#
# Regenerate with https://glad.sh using exactly these settings:
#   Generator/language : C/C++
#   Specification      : gl
#   API                : gl, version 3.3, profile Core
#   Extensions         : none
#   Options            : none (in particular NOT "loader")
#
# Why no extensions: every GL call the GUI frontend makes is core 3.3
# (shaders, VAO/VBO, textures, glPixelStorei, glBlendFunc), so a bigger
# header would only promise capabilities nothing checks for.
# Why no loader: SDL already exposes SDL_GL_GetProcAddress, and the glad
# documentation says to use the windowing library's loader when there is one.
# Ticking it would add a dlopen/dlsym path that duplicates SDL's.
#
# The generated files keep their own SPDX header, so provenance travels with
# them even if this comment is lost.
add_library(jot_glad STATIC
  src/gl.c
)
target_include_directories(jot_glad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(jot_glad PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
