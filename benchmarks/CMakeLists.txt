add_executable(jot_benchmarks
  benchmark_main.cpp
  ${PROJECT_SOURCE_DIR}/src/jot/event/in_memory_line_provider.cpp
  ${PROJECT_SOURCE_DIR}/src/jot/event/lazy_line_provider.cpp
  ${PROJECT_SOURCE_DIR}/src/features/folding.cpp
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
