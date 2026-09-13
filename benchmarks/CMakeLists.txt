add_executable(jot_benchmarks
  benchmark_main.cpp
  ${PROJECT_SOURCE_DIR}/src/jot/event/in_memory_line_provider.cpp
  ${PROJECT_SOURCE_DIR}/src/jot/event/lazy_line_provider.cpp
  ${PROJECT_SOURCE_DIR}/src/features/folding.cpp
  ${PROJECT_SOURCE_DIR}/src/features/language.cpp
  ${PROJECT_SOURCE_DIR}/src/tools/symbols/index.cpp
  ${PROJECT_SOURCE_DIR}/src/tools/workspace/search.cpp
  ${PROJECT_SOURCE_DIR}/src/ui/text.cpp
)

target_include_directories(jot_benchmarks PRIVATE
  ${PROJECT_SOURCE_DIR}/benchmarks
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/jot
  ${PROJECT_SOURCE_DIR}/src/features
  ${PROJECT_SOURCE_DIR}/src/tools
  ${PROJECT_SOURCE_DIR}/src/ui
)

# ui/text.cpp is compiled straight into this target (not via the engine), so
# utf8proc has to be linked here too: without it the target fails to link on
# utf8proc_map/utf8proc_iterate. The variables are published by
# src/CMakeLists.txt via PARENT_SCOPE.
target_include_directories(jot_benchmarks SYSTEM PRIVATE ${UTF8PROC_INCLUDE_DIRS})
if(UTF8PROC_LIBRARY_DIRS)
  target_link_directories(jot_benchmarks PRIVATE ${UTF8PROC_LIBRARY_DIRS})
endif()
target_link_libraries(jot_benchmarks PRIVATE ${UTF8PROC_LIBRARIES})
