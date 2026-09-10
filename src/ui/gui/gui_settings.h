// Settings overlay: an RmlUi (HTML/CSS-like layout engine) document that
// renders into the same GL context on top of the cell grid. The editor UI
// itself stays on the cell grid; this surface handles the settings dialog
// (theme, font size, cursor style) and is themed from jot's active palette
// through RCSS custom properties (var(--bg), var(--accent), ...).
//
// The overlay keeps RmlUi entirely behind this header (pimpl): gui.h stays
// SDL/RmlUi-free so terminal-only builds never see these types.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

class UIGui;

// Editor-side services (theme + persistent config live in Editor). Wired by
// Editor::initialize_gui_ui right after the GUI is created.
struct GuiSettingsCallbacks
{
  std::function<std::vector<std::string>()> list_themes;
  std::function<std::string()> current_theme;
  std::function<void(const std::string &)> apply_theme;
  std::function<std::string()> get_cursor_style;
  std::function<void(const std::string &)> set_cursor_style;
  // Resolves a theme group ("normal", "active_border", "selection", ...)
  // to xterm fg/bg indices (theme_group_color); false when unknown.
  std::function<bool(const std::string &, int &, int &)> theme_color;
  // Applies a font size in px to the editor grid, persists it, and tells
  // the editor to relayout panes (the same work the Ctrl+= pump does).
  std::function<void(int)> set_font_size;
};

class GuiSettingsOverlay
{
public:
  explicit GuiSettingsOverlay(UIGui *gui);
  ~GuiSettingsOverlay();

  // Non-copyable (owns RmlUi resources).
  GuiSettingsOverlay(const GuiSettingsOverlay &) = delete;
  GuiSettingsOverlay &operator=(const GuiSettingsOverlay &) = delete;

  bool is_open() const;
  void toggle();
  void open();
  void close();

  // Called every rendered frame while the overlay is open (and cheaply
  // skipped otherwise): advances RmlUi animations/layout and draws the
  // document into the current GL context, just before the swap.
  void update();
  void render();

  // Keeps the RmlUi viewport + context size in sync with the window
  // (drawable pixels). Called from UIGui::render each frame.
  void sync_viewport(int pixel_w, int pixel_h);

  // Event routing: called from UIGui::poll_event before the SDL event is
  // translated for the editor. `ev` is a const SDL_Event* (kept opaque so
  // this header never sees SDL's union type). Returns true when the
  // overlay consumed the event (mouse over the panel, typing in a control,
  // Esc to close), so the editor never sees it.
  bool handle_event(const void *ev);

  GuiSettingsCallbacks callbacks;

private:
  UIGui *gui_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};