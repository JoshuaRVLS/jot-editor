#ifndef KEYBIND_CATALOG_H
#define KEYBIND_CATALOG_H

// Built-in keybinding catalog + pure row composition for the which-key
// helper's "held modifier" view (hold Ctrl → see every Ctrl+… binding).
//
// Kept free of Editor/Lua types so it is unit-testable and cheap to include:
// callers translate live plugin keymaps into KeymapRef entries first.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace jot
{

  // One built-in Ctrl+<key> binding. The editor's modeless input handler owns
  // the actual dispatch; this table only powers the helper's display.
  struct BuiltinChord
  {
    char key; // letter as typed with Ctrl held ("s", "/", ' ')
    const char *detail;
  };

  // Built-in Ctrl bindings, mirroring the switch in
  // Editor::handle_modeless_input. Ctrl+Shift chords need Shift, so they are
  // not part of the held-Ctrl view.
  inline const BuiltinChord kBuiltinCtrlChords[] = {
      {'q', "Close pane / quit"},
      {'s', "Save"},
      {'z', "Undo"},
      {'y', "Redo"},
      {'a', "Select all"},
      {'c', "Copy"},
      {'x', "Cut"},
      {'v', "Paste"},
      {'b', "Toggle explorer"},
      {'e', "Open file picker"},
      {'f', "Find in file"},
      {'r', "Recent files"},
      {'g', "Go to line"},
      {'p', "Command palette"},
      {'m', "Toggle minimap"},
      {'t', "Choose theme"},
      {'d', "Duplicate line"},
      {'k', "Delete line"},
      {'/', "Toggle comment"},
      {'h', "Delete word backward"},
      {'w', "Delete word backward"},
      {' ', "Trigger completion"},
  };

  // A live plugin keymap, projected to the fields the composer needs.
  struct KeymapRef
  {
    std::string key; // canonical key, e.g. "Ctrl+T N"
    std::string detail;
    bool has_action; // callback or command registered
  };

  // One row of the held-modifier helper.
  struct KeybindRow
  {
    std::string key; // next key after the modifier, e.g. "S"
    std::string detail;
    bool group; // pressing it opens a subgroup instead of running an action
  };

  namespace keybind_detail
  {
    inline std::vector<std::string> split_steps(const std::string &key)
    {
      std::vector<std::string> steps;
      size_t start = 0;
      while (start <= key.size())
      {
        const size_t sp = key.find(' ', start);
        const size_t end = (sp == std::string::npos) ? key.size() : sp;
        if (end > start)
        {
          steps.push_back(key.substr(start, end - start));
        }
        start = end + 1;
      }
      return steps;
    }

    // Returns the letter after "Ctrl+" for a plugin chord, or empty when the
    // keymap is not a Ctrl chord the held view covers. "Ctrl+Space" maps to
    // the same row as the built-in space binding.
    inline std::string ctrl_letter(const std::string &chord)
    {
      if (chord.size() <= 5 || chord.compare(0, 5, "Ctrl+") != 0)
      {
        return "";
      }
      std::string letter = chord.substr(5);
      if (letter == "Space")
      {
        letter = " ";
      }
      return letter.size() == 1 ? letter : "";
    }
  } // namespace keybind_detail

  // Composes the rows shown while Ctrl is held: every Ctrl+… binding. Lua
  // keymaps override built-ins for the same chord; sequences ("Ctrl+T N")
  // turn their chord into a subgroup row unless a bare Ctrl+chord action also
  // exists (which wins, mirroring the editor's dispatch order).
  inline std::vector<KeybindRow> compose_ctrl_rows(const std::vector<KeymapRef> &plugins)
  {
    struct Info
    {
      std::string builtin_detail;
      std::string leaf_detail;
      bool leaf = false;
      std::string seq_detail;
      bool seq = false;
    };
    std::vector<std::pair<std::string, Info>> items;

    auto find_item = [&items](const std::string &letter) -> Info *
    {
      for (auto &item : items)
      {
        if (item.first == letter)
        {
          return &item.second;
        }
      }
      return nullptr;
    };
    auto ensure_item = [&items, &find_item](const std::string &letter) -> Info *
    {
      if (Info *info = find_item(letter))
      {
        return info;
      }
      items.push_back({letter, Info{}});
      return &items.back().second;
    };

    for (const auto &b : kBuiltinCtrlChords)
    {
      std::string letter(1, (char)std::toupper((unsigned char)b.key));
      ensure_item(letter)->builtin_detail = b.detail;
    }
    for (const auto &p : plugins)
    {
      const auto steps = keybind_detail::split_steps(p.key);
      if (steps.empty())
      {
        continue;
      }
      const std::string letter = keybind_detail::ctrl_letter(steps[0]);
      if (letter.empty())
      {
        continue;
      }
      Info *info = ensure_item(letter);
      if (steps.size() > 1)
      {
        info->seq = true;
        if (info->seq_detail.empty())
        {
          info->seq_detail = p.detail;
        }
      }
      else if (p.has_action)
      {
        info->leaf = true;
        info->leaf_detail = p.detail;
      }
    }

    std::vector<KeybindRow> out;
    for (const auto &item : items)
    {
      const Info &info = item.second;
      KeybindRow row;
      row.key = item.first;
      if (info.leaf)
      {
        row.group = false;
        row.detail = !info.leaf_detail.empty() ? info.leaf_detail : info.builtin_detail;
      }
      else if (info.seq)
      {
        row.group = true;
        row.detail = !info.seq_detail.empty() ? info.seq_detail : info.builtin_detail;
      }
      else
      {
        row.group = false;
        row.detail = info.builtin_detail;
      }
      out.push_back(std::move(row));
    }

    std::sort(out.begin(), out.end(),
              [](const KeybindRow &a, const KeybindRow &b) { return a.key < b.key; });
    return out;
  }

} // namespace jot

#endif
