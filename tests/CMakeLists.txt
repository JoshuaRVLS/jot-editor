add_executable(jot_tests
  test_main.cpp
  test_features.cpp
  test_cpp_assist.cpp
  test_telescope.cpp
)

target_link_libraries(jot_tests PRIVATE jot_features jot_python_bridge jot_tools)

target_include_directories(jot_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
)

add_test(NAME jot_features COMMAND jot_tests)
