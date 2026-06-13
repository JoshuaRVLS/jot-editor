add_executable(jot_tests
  test_main.cpp
  test_productivity.cpp
  test_folding.cpp
  test_features.cpp
  test_cpp_assist.cpp
  test_debugger.cpp
  test_syntax.cpp
  test_tree_sitter_install.cpp
  test_telescope.cpp
  test_ui_text.cpp
)

target_link_libraries(jot_tests PRIVATE jot_features jot_python_bridge jot_tools jot_ui)

target_include_directories(jot_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
)

add_test(NAME jot_features COMMAND jot_tests)
