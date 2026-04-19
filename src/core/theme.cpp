#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>

namespace {
std::string to_lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

std::string trim_copy(const std::string &s) {
  const size_t start = s.find_first_not_of(" \t");
  if (start == std::string::npos) {
    return "";
  }
  const size_t end = s.find_last_not_of(" \t");
  return s.substr(start, end - start + 1);
}
} // namespace

std::vector<std::string> Editor::list_available_themes() {
  std::vector<std::string> themes;
  if (python_api) {
    themes = python_api->py_list_themes();
  }

  if (themes.empty()) {
    themes.push_back("jot_nvim");
    return themes;
  }

  std::sort(themes.begin(), themes.end(),
            [](const std::string &a, const std::string &b) {
              return to_lower_copy(a) < to_lower_copy(b);
            });

  auto unique_end = std::unique(
      themes.begin(), themes.end(), [](const std::string &a, const std::string &b) {
        return to_lower_copy(a) == to_lower_copy(b);
      });
  themes.erase(unique_end, themes.end());
  return themes;
}

void Editor::apply_theme(const std::string &name, bool persist, bool announce) {
  const std::string requested = trim_copy(name);
  if (requested.empty()) {
    set_message("Theme name is empty");
    return;
  }

  std::string resolved = requested;
  const std::string needle = to_lower_copy(requested);
  const auto themes = list_available_themes();
  for (const auto &theme_name : themes) {
    if (to_lower_copy(theme_name) == needle) {
      resolved = theme_name;
      break;
    }
  }

  if (!python_api) {
    set_message("Python runtime unavailable for themes");
    return;
  }

  Theme previous_theme = theme;
  const std::string previous_theme_name = current_theme_name;

  // Start from defaults, then let Python colorscheme override highlight groups.
  theme = Theme();
  if (!python_api->py_apply_colorscheme(resolved)) {
    theme = previous_theme;
    current_theme_name = previous_theme_name;
    set_message("Unknown theme: " + requested);
    return;
  }

  current_theme_name = resolved;
  if (persist) {
    config.set("color_scheme", resolved);
  }

  needs_redraw = true;
  if (announce) {
    set_message("Theme: " + resolved);
  }
}
