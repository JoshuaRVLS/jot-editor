add_executable(jot_tests
  test_productivity.cpp
  test_folding.cpp
  test_features.cpp
  test_cpp_assist.cpp
  test_debugger.cpp
  test_imageviewer.cpp
  test_syntax.cpp
  test_terminal.cpp
  test_tree_sitter_install.cpp
  test_telescope.cpp
  test_ui_text.cpp
)

target_link_libraries(jot_tests PRIVATE
  Catch2::Catch2WithMain
  jot_features
  jot_python_bridge
  jot_tools
  jot_ui
)

target_include_directories(jot_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
)

include(Catch)
catch_discover_tests(jot_tests)
