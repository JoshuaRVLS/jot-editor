#include "command_utils.h"
#include "editor.h"

#include <vector>

using namespace CommandLineUtils;

void Editor::show_command_help(const std::string &topic_text) {
  const std::string topic = to_lower_copy(trim_copy(topic_text));
  if (topic == "commands" || topic == "cmd" || topic == "ex") {
    set_message(
        "Commands: :w :q :wq :e <file> :find [dir] :mkfile <p> :mkdir <p> "
        ":rename <old> <new> :rm <p> :cpppair <p> :cppimpl [p] "
        ":line N[:C] :bd :sp "
        "[left|right|up|down] :vsp [left|right] "
        ":splitleft/:splitright/:splitup/:splitdown :bn :bp :home "
        ":resume :recent :openrecent [n] :reopen :autosave [on/off/ms] "
        ":grep <text> :diagnostics :diagnext :diagprev :symbols :outline "
        ":format :trim "
        ":trimblank :upper :lower :sortlines :sortdesc :reverselines "
        ":uniquelines :shufflelines :joinlines :dupe :copypath :copyname "
        ":datetime :stats :replace :replacei :replaceword :replacere "
        ":surround :unsurround :fold :unfold :togglefold :foldall "
        ":unfoldall :incnum :decnum :lspstart :lspstatus "
        ":lspstop :lsprestart :tsinstall <language> :tsstatus :tsreload "
        ":task [name] :tasknew <name> :taskrerun "
        ":debug <program> :debugconfig [name] :debugstop "
        ":debugcontinue :debugnext :debugstep :debugout "
        ":gitstatus :gitdiff [file] :gitdiffstaged [file] "
        ":gitstage [file] :gitunstage [file] :gitstageall "
        ":gitunstageall :gitcommit <message> :gitlog :gitblame "
        ":gitrefresh :theme <name>");
    return;
  }

  std::vector<std::string> lines = {
      "Jot Keybind Help",
      "",
      "General",
      "  Ctrl+S           Save file",
      "  Ctrl+Q           Close pane (quit if single pane, with prompt)",
      "  Ctrl+P           Command palette",
      "  Ctrl+F           Search panel",
      "  Ctrl+B           Toggle file explorer",
      "  Ctrl+E           Telescope file finder",
      "  Ctrl+Shift+F     Search across workspace",
      "  Ctrl+Shift+M     Diagnostics picker",
      "  Ctrl+Shift+O     Document symbols",
      "  Ctrl+T           Theme chooser",
      "  Ctrl+M           Toggle minimap",
      "  Ctrl+X / Ctrl+`  Open/focus/minimize terminal",
      "  :task [name]     Run local/global terminal task",
      "",
      "Editing",
      "  Ctrl+Z / Ctrl+Y  Undo / Redo",
      "  Ctrl+A           Select all",
      "  Ctrl+C/X/V       Copy / Cut / Paste",
      "  Ctrl+D           Duplicate line",
      "  Ctrl+K           Delete line",
      "  Ctrl+/           Toggle comment",
      "  Ctrl+Backspace   Delete previous word",
      "  Ctrl+Shift+U     Uppercase selection/word",
      "  Ctrl+Shift+N     Lowercase selection/word",
      "  Shift+Arrows     Expand selection",
      "  Ctrl+Space       LSP completion",
      "",
      "Tabs",
      "  Ctrl+Tab          Next tab in current pane",
      "  Ctrl+Shift+Tab    Previous tab in current pane",
      "  Alt+, / Alt+.     Previous / Next tab",
      "  Alt+1..9 / Alt+0  Go to tab 1..9 / last tab",
      "",
      "Pane & Layout",
      "  Ctrl+Alt+H/J/K/L   Split left/down/up/right",
      "  Ctrl+Alt+Arrows    Focus pane",
      "  Ctrl+Alt+Q         Close current pane",
      "  Ctrl+Shift+H/J/K/L Resize pane",
      "  Ctrl+Arrow         Resize pane",
      "",
      "Power (Alt)",
      "  Alt+W              Close file tab",
      "  Alt+N              New buffer",
      "  Alt+S              Save",
      "  Alt+F              Search",
      "  Alt+E / Alt+Shift+E Next / Previous diagnostic",
      "  Alt+P              Command palette",
      "  Alt+B              Toggle explorer",
      "  Alt+M              Toggle minimap",
      "  Alt+T              Theme chooser",
      "  Alt+U / Alt+N      Uppercase / Lowercase",
      "  Alt+O              Sort selected lines",
      "  Alt+Up / Alt+Down  Move line up/down",
      "  Alt+H / Alt+L      Move word left/right",
      "  Alt+I / Alt+A      Smart line start / line end",
      "  Alt+G / Alt+Shift+G File start / file end",
      "",
      "Tips",
      "  :help commands     Show ex-command summary",
      "  :help              Show this keybind help",
      "  Extra commands: :sortdesc :reverselines :uniquelines",
      "                  :shufflelines :joinlines :dupe :trimblank",
      "                  :copypath :copyname :datetime :stats",
      "                  :replace :replacei :replaceword :replacere",
      "                  :surround :unsurround :incnum :decnum"};

  std::string out;
  for (size_t i = 0; i < lines.size(); i++) {
    out += lines[i];
    if (i + 1 < lines.size()) {
      out.push_back('\n');
    }
  }
  show_popup(out, 2, tab_height + 1);
}
