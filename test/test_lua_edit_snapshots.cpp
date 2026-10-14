// The per-file edit snapshots behind BufChange deltas copy every line of their
// file, so LuaAPI bounds the total by lines as well as by file count: a few
// large open buffers would otherwise keep tens of MB alive for deltas a plugin
// may never read. This drives the real on_buffer_change path with a BufChange
// listener registered and pins both the accounting and the eviction.
#include "core/editor.h"
#include "lua/api/api.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>

namespace
{
  // A pristine config home so the Editor constructor's config bootstrap does
  // not read the real user config (which would call into an unhosted Lua
  // state).
  void use_temp_config_home()
  {
    static bool seeded = false;
    if (seeded)
    {
      return;
    }
    char cfgdir[] = "/tmp/jot_edit_snapshot_test_XXXXXX";
    mkdtemp(cfgdir);
    setenv("JOT_CONFIG_HOME", cfgdir, 1);
    setenv("JOT_CACHE_HOME", cfgdir, 1);
    seeded = true;
  }

  // A fresh named buffer of `lines` identical lines, returned as its index.
  int add_buffer(Editor &e, const std::string &name, std::size_t lines)
  {
    e.create_new_buffer_for_test();
    FileBuffer &buf = e.buffer_for_test();
    buf.filepath = "/tmp/jot_edit_snapshots/" + name;
    buf.lines.assign(lines, "int value = 0;");
    return e.buffer_count_for_test() - 1;
  }

  void edit(LuaAPI &api, Editor &e, int index)
  {
    api.on_buffer_change(e.buffer_for_test(index).filepath, "");
  }
} // namespace

TEST_CASE("BufChange snapshots stay inside the line budget")
{
  use_temp_config_home();
  Editor e;
  LuaAPI api(&e);
  api.register_autocmd("BufChange", "", "", false);

  // Each edit snapshots its file: one copied line per line.
  const int a = add_buffer(e, "a.txt", 3000);
  edit(api, e, a);
  REQUIRE(api.edit_snapshot_lines_for_test() == 3000);

  // A second file under the budget keeps both snapshots.
  const int b = add_buffer(e, "b.txt", 3000);
  edit(api, e, b);
  REQUIRE(api.edit_snapshot_lines_for_test() == 6000);

  // Two 100k-line files: the total stays capped at 200k lines, and the file
  // that just changed keeps its snapshot (its next edit only refreshes it).
  const int c = add_buffer(e, "c.txt", 100000);
  edit(api, e, c);
  REQUIRE(api.edit_snapshot_lines_for_test() == 106000);
  const int d = add_buffer(e, "d.txt", 100000);
  edit(api, e, d);
  REQUIRE(api.edit_snapshot_lines_for_test() == 200000);
  edit(api, e, d);
  REQUIRE(api.edit_snapshot_lines_for_test() == 200000);

  // Both 100k-line files survive because the two small, older snapshots paid
  // for the budget: a sweep drops the file left alone longest, never the one
  // that just changed.
  const std::string c_path = e.buffer_for_test(c).filepath;
  const std::string d_path = e.buffer_for_test(d).filepath;
  REQUIRE_FALSE(api.edit_snapshot_has_for_test(e.buffer_for_test(a).filepath));
  REQUIRE_FALSE(api.edit_snapshot_has_for_test(e.buffer_for_test(b).filepath));
  REQUIRE(api.edit_snapshot_has_for_test(c_path));
  REQUIRE(api.edit_snapshot_has_for_test(d_path));

  // Editing an evicted file again snapshots it afresh, and the sweep then takes
  // the file left alone longest (c, older than d) instead of the new one.
  edit(api, e, a);
  REQUIRE(api.edit_snapshot_has_for_test(e.buffer_for_test(a).filepath));
  REQUIRE(api.edit_snapshot_lines_for_test() == 103000);
  REQUIRE_FALSE(api.edit_snapshot_has_for_test(c_path));
  REQUIRE(api.edit_snapshot_has_for_test(d_path));

  // A file past the per-file guard (>100k lines) is not snapshotted at all,
  // so it cannot push the total over the budget either.
  const std::size_t before = api.edit_snapshot_lines_for_test();
  const int huge = add_buffer(e, "huge.txt", 100001);
  edit(api, e, huge);
  REQUIRE(api.edit_snapshot_lines_for_test() == before);
}
