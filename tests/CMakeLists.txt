add_executable(jot_tests
  test_main.cpp
  test_features.cpp
)

target_link_libraries(jot_tests PRIVATE jot_features)

target_include_directories(jot_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/tests
)

add_test(NAME jot_features COMMAND jot_tests)
