#include "lua_bridge/api.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Lua scratch buffers keep stable handles and line ranges") {
  LuaAPI api(nullptr);
  const int first = api.create_scratch_buffer(false, true);
  const int second = api.create_scratch_buffer(false, true);
  REQUIRE(first != second);
  REQUIRE(api.set_scratch_lines(first, 0, -1, true, {"one", "two", "three"}));
  REQUIRE(api.get_scratch_lines(first, 1, 3, true) ==
          std::vector<std::string>{"two", "three"});
  REQUIRE(api.set_scratch_lines(first, 1, 2, true, {"replaced"}));
  REQUIRE(api.get_scratch_lines(first, 0, -1, true) ==
          std::vector<std::string>{"one", "replaced", "three"});
  REQUIRE(api.delete_scratch_buffer(first));
  REQUIRE_FALSE(api.delete_scratch_buffer(first));
  REQUIRE(api.is_float_valid(0) == false);
}
