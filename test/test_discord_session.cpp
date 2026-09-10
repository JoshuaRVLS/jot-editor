// Discord presence session lifecycle (src/jot/app/discord_session.cpp): the
// enabled / excluded / idle gating around the IPC client, driven through the
// same entry points the editor's timer, focus reporting and :discord use.
//
// No Discord is involved: the tests point XDG_RUNTIME_DIR at a scratch
// directory, so nothing can reach a real client running on the machine.
#include "editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>

namespace
{
  // A scratch runtime directory with no discord-ipc socket: the session stays
  // disconnected, which is exactly what these tests need.
  std::string scratch_runtime_dir()
  {
    static std::string dir;
    if (dir.empty())
    {
      char tmpl[] = "/tmp/jot_discord_session_XXXXXX";
      REQUIRE(mkdtemp(tmpl) != nullptr);
      dir = tmpl;
    }
    return dir;
  }

  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_discord_cfg_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      // Ensure the client cannot find a real Discord socket: XDG_RUNTIME_DIR is
      // probed first and holds nothing.
      setenv("XDG_RUNTIME_DIR", scratch_runtime_dir().c_str(), 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }
} // namespace

TEST_CASE("Discord presence session follows the enabled setting", "[jot]")
{
  Editor &e = probe_editor();
  // The presence is only (re)sent when its content changes, and the signature
  // outlives a test case: start from the same state :discord reconnect creates.
  e.discord_command_for_test("reconnect");
  e.config_set_for_test("discord_rpc", "false");
  e.config_set_for_test("discord_exclude_workspaces", "");
  e.discord_poll_for_test(1000);
  REQUIRE(e.discord_status_for_test() == "off");
  REQUIRE_FALSE(e.discord_pending_activity_for_test());

  e.config_set_for_test("discord_rpc", "true");
  e.discord_poll_for_test(2000);
  // Enabled but nothing to talk to: the session reports "connecting" and the
  // activity is queued for the moment a connection exists.
  REQUIRE(e.discord_status_for_test() == "connecting");
  REQUIRE_FALSE(e.discord_connected_for_test());
  REQUIRE(e.discord_pending_activity_for_test());
}

TEST_CASE("Discord presence session honors workspace exclude patterns", "[jot]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("discord_rpc", "true");
  e.config_set_for_test("discord_exclude_workspaces", "this-does-not-match,.*");
  e.discord_poll_for_test(1000);
  REQUIRE(e.discord_status_for_test() == "excluded");
  REQUIRE_FALSE(e.discord_pending_activity_for_test());

  // A malformed pattern is reported (through :discord status) instead of
  // silently disabling the presence.
  e.config_set_for_test("discord_exclude_workspaces", "[unclosed");
  e.discord_poll_for_test(2000);
  REQUIRE(e.discord_status_for_test() != "excluded");
  REQUIRE(e.discord_command_for_test("status").find("[unclosed") != std::string::npos);

  e.config_set_for_test("discord_exclude_workspaces", "");
}

TEST_CASE("Discord status and assets report what to upload", "[jot]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("discord_rpc", "true");
  e.config_set_for_test("discord_exclude_workspaces", "");
  e.config_set_for_test("discord_app_id", "123456");

  // No file open: the idling artwork plus the always-present badges.
  const std::string idle = e.discord_command_for_test("assets");
  REQUIRE(idle.find("jot") != std::string::npos);
  REQUIRE(idle.find("debug") != std::string::npos);
  REQUIRE(idle.find("123456") != std::string::npos);
  REQUIRE(idle.find("/rich-presence/assets") != std::string::npos);
  REQUIRE(idle.find("ASSETS.md") != std::string::npos);

  // The status line reports the state and the app id, and points at the upload
  // path when Discord complains about an asset.
  const std::string status = e.discord_command_for_test("status");
  REQUIRE(status.find("Discord presence:") != std::string::npos);
  REQUIRE(status.find("app id 123456") != std::string::npos);
  REQUIRE(status.find(":discord assets") == std::string::npos); // no asset error yet
}

TEST_CASE("Discord status is visible in the status line", "[jot]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("discord_show_status", "true");
  e.config_set_for_test("discord_exclude_workspaces", "");
  // Start from the disabled state so enabling produces a real status change
  // (the probe editor is shared across cases, so its status is not pristine).
  e.config_set_for_test("discord_rpc", "false");
  e.discord_poll_for_test(500);
  REQUIRE(e.discord_status_for_test() == "off");

  // A status change must request a repaint on its own: the poll runs on a
  // timer, so without this the chip would not show up until the user happened
  // to trigger a redraw by typing.
  e.config_set_for_test("discord_rpc", "true");
  e.clear_needs_redraw_for_test();
  e.discord_poll_for_test(1000);
  REQUIRE(e.discord_status_for_test() == "connecting");
  REQUIRE(e.needs_redraw_for_test());

  // And the chip really is painted: scan the status rows for the label. No
  // Discord runs here, so the session reports the connecting state.
  e.render_for_test();
  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);
  const int height = e.ui_height_for_test();
  const auto row_text = [&](int y)
  {
    std::string text;
    for (int x = 0; x < e.ui_width_for_test(); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      text += cell ? cell->ch : " ";
    }
    return text;
  };
  std::string status_rows;
  for (int y = height - 2; y < height; y++)
  {
    status_rows += "\n  row " + std::to_string(y) + ": [" + row_text(y) + "]";
  }
  INFO("status rows:" << status_rows);
  REQUIRE(status_rows.find("Discord") != std::string::npos);

  // No cell in the status rows may hold a control character: the status line is
  // assembled from byte-offset spans, and a stray newline would move the
  // terminal cursor and shear the row apart.
  for (int y = height - 2; y < height; y++)
  {
    for (int x = 0; x < e.ui_width_for_test(); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      if (cell && (cell->ch == "\n" || cell->ch == "\r" || cell->ch == "\t"))
      {
        INFO("control character cell at column " << x << " row " << y);
        REQUIRE(cell->ch == " ");
      }
    }
  }

  // Turning the chip off removes it (the config change dirties the frame the
  // way apply_config_live does).
  e.config_set_for_test("discord_show_status", "false");
  e.request_redraw_for_test();
  e.render_for_test();
  std::string hidden_rows;
  for (int y = height - 2; y < height; y++)
  {
    hidden_rows += row_text(y);
  }
  REQUIRE(hidden_rows.find("Discord") == std::string::npos);

  e.config_set_for_test("discord_show_status", "true");
  e.render_for_test();
}

TEST_CASE("Discord presence session clears and restores across idle periods", "[jot]")
{
  Editor &e = probe_editor();
  e.discord_command_for_test("reconnect");
  e.config_set_for_test("discord_rpc", "true");
  e.config_set_for_test("discord_exclude_workspaces", "");
  e.config_set_for_test("discord_idle_timeout", "2");

  // Focused: the presence is queued.
  e.discord_focus_for_test(true, 1000);
  e.discord_poll_for_test(1000);
  REQUIRE_FALSE(e.discord_idle_cleared_for_test());
  REQUIRE(e.discord_pending_activity_for_test());

  // Away for longer than the timeout: the profile is cleared. The clock is
  // synthetic and shared by focus stamps and polls, exactly like the real one.
  e.discord_focus_for_test(false, 1500);
  e.discord_poll_for_test(2000); // 0.5s away: still up
  REQUIRE_FALSE(e.discord_idle_cleared_for_test());
  e.discord_poll_for_test(5000); // 3.5s away: past the 2s timeout
  REQUIRE(e.discord_idle_cleared_for_test());
  REQUIRE(e.discord_status_for_test() == "idle");
  REQUIRE_FALSE(e.discord_pending_activity_for_test());

  // Coming back restores it: the queued presence is re-sent even though the
  // content itself never changed.
  e.discord_focus_for_test(true, 6000);
  e.discord_poll_for_test(6000);
  REQUIRE_FALSE(e.discord_idle_cleared_for_test());
  REQUIRE(e.discord_pending_activity_for_test());

  // A zero timeout means "never clear" (upstream's default).
  e.config_set_for_test("discord_idle_timeout", "0");
  e.discord_focus_for_test(false, 7000);
  e.discord_poll_for_test(600000);
  REQUIRE_FALSE(e.discord_idle_cleared_for_test());
  e.discord_focus_for_test(true, 600001);
}
