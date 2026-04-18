add_executable(jot main.cpp)

target_link_libraries(jot PRIVATE
  jot_engine
  stdc++fs
  util
  ${PYTHON_LDFLAGS_LIST}
)
