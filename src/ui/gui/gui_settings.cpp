// Settings overlay implementation. Owns one RmlUi context whose document
// (runtime/gui/settings.rml + settings.rcss, resolved through a file
// interface rooted at JOT_GUI_SOURCE_DIR) renders into the GUI's GL
// context on top of the cell grid. The document is themed from jot's
// active palette through RCSS custom properties (var(--bg), ...) that
// this module refreshes whenever the theme changes.
#include "gui/gui_settings.h"
#include "gui/gui.h"

#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>

#include "RmlUi_Renderer_BackwardCompatible_GL3.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef JOT_GUI_SOURCE_DIR
#define JOT_GUI_SOURCE_DIR "."
#endif

namespace
{
// --- RmlUi system interface: SDL clock + stderr logging. ---
class JotSystemInterface final : public Rml::SystemInterface
{
public:
  double GetElapsedTime() override
  {
    return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
  }

  bool LogMessage(Rml::Log::Type, const Rml::String &message) override
  {
    std::fprintf(stderr, "rmlui: %s\n", message.c_str());
    return true;
  }
};

// --- RmlUi file interface rooted at the runtime/gui asset directory. ---
class JotFileInterface final : public Rml::FileInterface
{
public:
  explicit JotFileInterface(Rml::String base) : base_(std::move(base)) {}

  Rml::FileHandle Open(const Rml::String &path) override
  {
    FILE *f = std::fopen(resolve(path).c_str(), "rb");
    return (Rml::FileHandle)f;
  }
  void Close(Rml::FileHandle handle) override
  {
    if (handle)
      std::fclose((FILE *)handle);
  }
  size_t Read(void *buffer, size_t size, Rml::FileHandle handle) override
  {
    return handle ? std::fread(buffer, 1, size, (FILE *)handle) : 0;
  }
  bool Seek(Rml::FileHandle handle, long offset, int origin) override
  {
    return handle && std::fseek((FILE *)handle, offset, origin) == 0;
  }
  size_t Tell(Rml::FileHandle handle) override
  {
    return handle ? (size_t)std::ftell((FILE *)handle) : 0;
  }
  size_t Length(Rml::FileHandle handle) override
  {
    if (!handle)
      return 0;
    FILE *f = (FILE *)handle;
    const long cur = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, cur, SEEK_SET);
    return (size_t)len;
  }

private:
  Rml::String resolve(const Rml::String &path) const
  {
    if (path.empty())
      return base_;
    if (path[0] == '/')
      return path;
    return base_ + "/" + path;
  }

  Rml::String base_;
};

// --- Font discovery: same candidates as UIGui::init_freetype. ---
std::string find_regular_font()
{
  const char *home = std::getenv("HOME");
  const std::string base = home ? std::string(home) + "/.local/share/fonts" : "";
  const std::vector<std::string> candidates = {
      base + "/JetBrainsMonoNerdFontMono-Regular.ttf",
      "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Regular.ttf",
      "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
  };
  for (const std::string &path : candidates)
  {
    if (!path.empty() && std::filesystem::exists(path))
      return path;
  }
  return "";
}

// --- SDL key -> RmlUi key identifier. ---
Rml::Input::KeyIdentifier sdl_to_rml_key(SDL_Keycode sym)
{
  if (sym >= SDLK_a && sym <= SDLK_z)
    return (Rml::Input::KeyIdentifier)(Rml::Input::KI_A + (sym - SDLK_a));
  if (sym >= SDLK_0 && sym <= SDLK_9)
    return (Rml::Input::KeyIdentifier)(Rml::Input::KI_0 + (sym - SDLK_0));
  if (sym >= SDLK_F1 && sym <= SDLK_F24)
    return (Rml::Input::KeyIdentifier)(Rml::Input::KI_F1 + (sym - SDLK_F1));
  if (sym >= SDLK_KP_1 && sym <= SDLK_KP_9)
    return (Rml::Input::KeyIdentifier)(Rml::Input::KI_NUMPAD1 + (sym - SDLK_KP_1));
  switch (sym)
  {
  case SDLK_SPACE: return Rml::Input::KI_SPACE;
  case SDLK_RETURN:
  case SDLK_KP_ENTER: return Rml::Input::KI_RETURN;
  case SDLK_BACKSPACE: return Rml::Input::KI_BACK;
  case SDLK_TAB: return Rml::Input::KI_TAB;
  case SDLK_ESCAPE: return Rml::Input::KI_ESCAPE;
  case SDLK_DELETE: return Rml::Input::KI_DELETE;
  case SDLK_INSERT: return Rml::Input::KI_INSERT;
  case SDLK_HOME: return Rml::Input::KI_HOME;
  case SDLK_END: return Rml::Input::KI_END;
  case SDLK_PAGEUP: return Rml::Input::KI_PRIOR;
  case SDLK_PAGEDOWN: return Rml::Input::KI_NEXT;
  case SDLK_LEFT: return Rml::Input::KI_LEFT;
  case SDLK_RIGHT: return Rml::Input::KI_RIGHT;
  case SDLK_UP: return Rml::Input::KI_UP;
  case SDLK_DOWN: return Rml::Input::KI_DOWN;
  case SDLK_SEMICOLON: return Rml::Input::KI_OEM_1;
  case SDLK_EQUALS: return Rml::Input::KI_OEM_PLUS;
  case SDLK_COMMA: return Rml::Input::KI_OEM_COMMA;
  case SDLK_MINUS: return Rml::Input::KI_OEM_MINUS;
  case SDLK_PERIOD: return Rml::Input::KI_OEM_PERIOD;
  case SDLK_SLASH: return Rml::Input::KI_OEM_2;
  case SDLK_BACKQUOTE: return Rml::Input::KI_OEM_3;
  case SDLK_LEFTBRACKET: return Rml::Input::KI_OEM_4;
  case SDLK_BACKSLASH: return Rml::Input::KI_OEM_5;
  case SDLK_RIGHTBRACKET: return Rml::Input::KI_OEM_6;
  case SDLK_QUOTE: return Rml::Input::KI_OEM_7;
  case SDLK_LSHIFT: return Rml::Input::KI_LSHIFT;
  case SDLK_RSHIFT: return Rml::Input::KI_RSHIFT;
  case SDLK_LCTRL: return Rml::Input::KI_LCONTROL;
  case SDLK_RCTRL: return Rml::Input::KI_RCONTROL;
  case SDLK_LALT: return Rml::Input::KI_LMENU;
  case SDLK_RALT: return Rml::Input::KI_RMENU;
  case SDLK_LGUI: return Rml::Input::KI_LWIN;
  case SDLK_RGUI: return Rml::Input::KI_RWIN;
  case SDLK_CAPSLOCK: return Rml::Input::KI_CAPITAL;
  case SDLK_NUMLOCKCLEAR: return Rml::Input::KI_NUMLOCK;
  default: return Rml::Input::KI_UNKNOWN;
  }
}

int sdl_mods(Uint16 mod)
{
  int out = 0;
  if (mod & KMOD_CTRL)
    out |= Rml::Input::KM_CTRL;
  if (mod & KMOD_SHIFT)
    out |= Rml::Input::KM_SHIFT;
  if (mod & KMOD_ALT)
    out |= Rml::Input::KM_ALT;
  if (mod & KMOD_GUI)
    out |= Rml::Input::KM_META;
  return out;
}

int mouse_button_index(Uint8 button)
{
  switch (button)
  {
  case SDL_BUTTON_LEFT: return 0;
  case SDL_BUTTON_RIGHT: return 1;
  case SDL_BUTTON_MIDDLE: return 2;
  default: return 0;
  }
}
} // namespace

struct GuiSettingsOverlay::Impl
{
  // Event listener dispatching into the overlay impl.
  class Listener final : public Rml::EventListener
  {
  public:
    explicit Listener(Impl *impl) : impl_(impl) {}
    void ProcessEvent(Rml::Event &event) override
    {
      if (impl_)
        impl_->on_event(event);
    }

  private:
    Impl *impl_;
  };

  explicit Impl(UIGui *owner) : gui(owner) {}

  ~Impl()
  {
    delete listener;
    delete files;
    delete sys;
    delete renderer;
  }

  void ensure_init();
  void load_document();
  void apply_palette();
  void populate_themes();
  void sync_controls();
  void on_event(Rml::Event &event);
  void close();

  UIGui *gui;
  Rml::Context *ctx = nullptr;
  Rml::ElementDocument *doc = nullptr;
  RenderInterface_BackwardCompatible_GL3 *renderer = nullptr;
  JotSystemInterface *sys = nullptr;
  JotFileInterface *files = nullptr;
  Listener *listener = nullptr;
  bool inited = false;
  bool open = false;
  int viewport_w = 0;
  int viewport_h = 0;
};

void GuiSettingsOverlay::Impl::ensure_init()
{
  if (inited)
    return;
  inited = true;

  renderer = new RenderInterface_BackwardCompatible_GL3();
  sys = new JotSystemInterface();
  files = new JotFileInterface(JOT_GUI_SOURCE_DIR);
  listener = new Listener(this);

  Rml::SetSystemInterface(sys);
  Rml::SetRenderInterface(renderer->GetAdaptedInterface());
  Rml::SetFileInterface(files);
  if (!Rml::Initialise())
  {
    std::fprintf(stderr, "rmlui: Initialise() failed; settings overlay disabled\n");
    return;
  }
  const std::string font = find_regular_font();
  if (font.empty())
  {
    std::fprintf(stderr, "rmlui: no font found for the settings overlay\n");
  }
  else
  {
    Rml::LoadFontFace(font, "jot-ui", Rml::Style::FontStyle::Normal);
  }
  int pw = 0, ph = 0;
  gui->drawable_size(pw, ph);
  ctx = Rml::CreateContext("settings", Rml::Vector2i(std::max(1, pw), std::max(1, ph)));
  if (!ctx)
  {
    std::fprintf(stderr, "rmlui: CreateContext failed; settings overlay disabled\n");
    return;
  }
  viewport_w = std::max(1, pw);
  viewport_h = std::max(1, ph);
  renderer->SetViewport(viewport_w, viewport_h);
}

void GuiSettingsOverlay::Impl::load_document()
{
  doc = ctx->LoadDocument("settings.rml");
  if (!doc)
  {
    std::fprintf(stderr, "rmlui: failed to load settings.rml from %s\n", JOT_GUI_SOURCE_DIR);
    return;
  }
  if (Rml::Element *theme = doc->GetElementById("theme"))
    theme->AddEventListener("change", listener, false);
  if (Rml::Element *cursor = doc->GetElementById("cursor-style"))
    cursor->AddEventListener("change", listener, false);
  if (Rml::Element *size = doc->GetElementById("font-size"))
  {
    size->AddEventListener("input", listener, false);
    size->AddEventListener("change", listener, false);
  }
  if (Rml::Element *close_btn = doc->GetElementById("close-btn"))
    close_btn->AddEventListener("click", listener, false);
  if (Rml::Element *done = doc->GetElementById("done-btn"))
    done->AddEventListener("click", listener, false);
}

void GuiSettingsOverlay::Impl::apply_palette()
{
  if (!doc || !gui)
    return;
  auto color = [this](const std::string &group, int fallback) -> std::string
  {
    int fg = -1, bg = -1;
    if (gui->settings().callbacks.theme_color && gui->settings().callbacks.theme_color(group, fg, bg))
    {
      if (fg >= 0)
        return UIGui::xterm_css_color(fg);
      if (bg >= 0)
        return UIGui::xterm_css_color(bg);
    }
    return UIGui::xterm_css_color(fallback);
  };
  Rml::Element *root = doc->GetElementById("settings-root");
  if (!root)
    return;
  root->SetProperty("--bg", color("normal", 0));
  root->SetProperty("--fg", color("normal", 7));
  root->SetProperty("--accent", color("active_border", 6));
  root->SetProperty("--border", color("panel_border", 8));
  root->SetProperty("--muted", color("comment", 8));
  int sel_fg = -1, sel_bg = -1;
  if (gui->settings().callbacks.theme_color &&
      gui->settings().callbacks.theme_color("selection", sel_fg, sel_bg) && sel_bg >= 0)
  {
    root->SetProperty("--sel-bg", UIGui::xterm_css_color(sel_bg));
  }
  else
  {
    root->SetProperty("--sel-bg", UIGui::xterm_css_color(0));
  }
}

void GuiSettingsOverlay::Impl::populate_themes()
{
  if (!doc)
    return;
  Rml::Element *select = doc->GetElementById("theme");
  if (!select)
    return;
  while (Rml::Element *child = select->GetFirstChild())
    select->RemoveChild(child);

  GuiSettingsCallbacks &cb = gui->settings().callbacks;
  std::vector<std::string> themes = cb.list_themes ? cb.list_themes() : std::vector<std::string>{};
  std::string current = cb.current_theme ? cb.current_theme() : "";
  for (const std::string &name : themes)
  {
    Rml::ElementPtr option = doc->CreateElement("option");
    option->SetAttribute("value", name);
    option->SetInnerRML(name);
    select->AppendChild(std::move(option));
  }
  auto *form = dynamic_cast<Rml::ElementFormControlSelect *>(select);
  if (form && !current.empty())
    form->SetValue(current);
}

void GuiSettingsOverlay::Impl::sync_controls()
{
  if (!doc)
    return;
  GuiSettingsCallbacks &cb = gui->settings().callbacks;
  // Font size slider + label.
  if (Rml::Element *size = doc->GetElementById("font-size"))
  {
    if (auto *input = dynamic_cast<Rml::ElementFormControl *>(size))
      input->SetValue(std::to_string(gui->font_px()));
  }
  if (Rml::Element *label = doc->GetElementById("font-size-value"))
    label->SetInnerRML(std::to_string(gui->font_px()) + " px");
  // Cursor style select.
  if (Rml::Element *cursor = doc->GetElementById("cursor-style"))
  {
    const std::string value = cb.get_cursor_style ? cb.get_cursor_style() : "bar";
    if (auto *form = dynamic_cast<Rml::ElementFormControlSelect *>(cursor))
      form->SetValue(value);
  }
}

void GuiSettingsOverlay::Impl::on_event(Rml::Event &event)
{
  Rml::Element *target = event.GetTargetElement();
  if (!target)
    return;
  GuiSettingsCallbacks &cb = gui->settings().callbacks;
  const Rml::String id = target->GetId();
  if (id == "theme")
  {
    auto *form = dynamic_cast<Rml::ElementFormControlSelect *>(target);
    if (form && cb.apply_theme)
    {
      cb.apply_theme(form->GetValue());
      apply_palette();
    }
  }
  else if (id == "cursor-style")
  {
    auto *form = dynamic_cast<Rml::ElementFormControlSelect *>(target);
    if (form && cb.set_cursor_style)
      cb.set_cursor_style(form->GetValue());
  }
  else if (id == "font-size")
  {
    auto *form = dynamic_cast<Rml::ElementFormControl *>(target);
    if (!form)
      return;
    const int px = std::atoi(form->GetValue().c_str());
    if (px >= 8 && px <= 40)
    {
      if (cb.set_font_size)
        cb.set_font_size(px);
      if (Rml::Element *label = doc->GetElementById("font-size-value"))
        label->SetInnerRML(std::to_string(px) + " px");
    }
  }
  else if (id == "close-btn" || id == "done-btn")
  {
    close();
  }
}

void GuiSettingsOverlay::Impl::close()
{
  if (doc)
    doc->Hide();
  open = false;
}

GuiSettingsOverlay::GuiSettingsOverlay(UIGui *gui)
    : gui_(gui), impl_(std::make_unique<Impl>(gui))
{
}

GuiSettingsOverlay::~GuiSettingsOverlay()
{
  if (impl_ && impl_->inited)
  {
    impl_->close();
    // Shutdown releases the context/documents; the system/render/file
    // interfaces must outlive it, so they are deleted afterwards (in the
    // Impl destructor).
    Rml::Shutdown();
  }
}

bool GuiSettingsOverlay::is_open() const
{
  return impl_ && impl_->open;
}

void GuiSettingsOverlay::toggle()
{
  if (is_open())
    close();
  else
    open();
}

void GuiSettingsOverlay::open()
{
  impl_->ensure_init();
  if (!impl_->ctx)
    return;
  if (!impl_->doc)
    impl_->load_document();
  if (!impl_->doc)
    return;
  impl_->apply_palette();
  impl_->populate_themes();
  impl_->sync_controls();
  impl_->doc->Show();
  impl_->open = true;
}

void GuiSettingsOverlay::close()
{
  if (impl_)
    impl_->close();
}

void GuiSettingsOverlay::update()
{
  if (!impl_ || !impl_->inited || !impl_->open || !impl_->ctx)
    return;
  impl_->ctx->Update();
}

void GuiSettingsOverlay::render()
{
  if (!impl_ || !impl_->inited || !impl_->open || !impl_->ctx || !impl_->renderer)
    return;
  impl_->renderer->BeginFrame();
  impl_->ctx->Render();
  impl_->renderer->EndFrame();
}

void GuiSettingsOverlay::sync_viewport(int pixel_w, int pixel_h)
{
  if (!impl_ || !impl_->inited || !impl_->ctx || !impl_->renderer)
    return;
  if (pixel_w == impl_->viewport_w && pixel_h == impl_->viewport_h)
    return;
  impl_->viewport_w = std::max(1, pixel_w);
  impl_->viewport_h = std::max(1, pixel_h);
  impl_->ctx->SetDimensions(Rml::Vector2i(impl_->viewport_w, impl_->viewport_h));
  impl_->renderer->SetViewport(impl_->viewport_w, impl_->viewport_h);
}

bool GuiSettingsOverlay::handle_event(const void *event_ptr)
{
  if (!is_open() || !impl_->ctx || !event_ptr)
    return false;
  const SDL_Event *ev = static_cast<const SDL_Event *>(event_ptr);

  // Point -> pixel scale (HiDPI): RmlUi works in drawable pixels.
  int pw = 0, ph = 0, dw = 0, dh = 0;
  gui_->window_size(pw, ph);
  gui_->drawable_size(dw, dh);
  const float sx = (float)dw / (float)std::max(1, pw);
  const float sy = (float)dh / (float)std::max(1, ph);

  switch (ev->type)
  {
  case SDL_MOUSEMOTION:
  {
    const int state = (ev->motion.state & SDL_BUTTON_LMASK) ? 1 : 0;
    impl_->ctx->ProcessMouseMove((int)(ev->motion.x * sx), (int)(ev->motion.y * sy), state);
    return true;
  }
  case SDL_MOUSEBUTTONDOWN:
    impl_->ctx->ProcessMouseButtonDown(mouse_button_index(ev->button.button),
                                       sdl_mods(ev->button.state));
    return true;
  case SDL_MOUSEBUTTONUP:
    impl_->ctx->ProcessMouseButtonUp(mouse_button_index(ev->button.button),
                                     sdl_mods(ev->button.state));
    return true;
  case SDL_MOUSEWHEEL:
    // RmlUi: negative = scroll up (X11 convention); SDL wheel.y: + = up.
    // SDL2's wheel event carries no modifier state; query it live.
    impl_->ctx->ProcessMouseWheel(-(float)ev->wheel.y, sdl_mods(SDL_GetModState()));
    return true;
  case SDL_KEYDOWN:
    if (ev->key.keysym.sym == SDLK_ESCAPE && !ev->key.repeat)
    {
      close();
      return true;
    }
    impl_->ctx->ProcessKeyDown(sdl_to_rml_key(ev->key.keysym.sym),
                               sdl_mods(ev->key.keysym.mod));
    return true;
  case SDL_KEYUP:
    impl_->ctx->ProcessKeyUp(sdl_to_rml_key(ev->key.keysym.sym), sdl_mods(ev->key.keysym.mod));
    return true;
  case SDL_TEXTINPUT:
    impl_->ctx->ProcessTextInput(ev->text.text);
    return true;
  default:
    // QUIT, window events etc. still flow to the editor.
    return false;
  }
}