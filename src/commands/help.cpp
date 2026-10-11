#include "commands/utils.h"
#include "core/editor.h"

#include <vector>

using namespace CommandLineUtils;

void Editor::show_command_help(const std::string &topic_text)
{
  const std::string topic = to_lower_copy(trim_copy(topic_text));
  if (topic == "commands" || topic == "cmd" || topic == "ex")
  {
    set_message("Commands: :w :q :wq :e <file> :find [dir] :mkfile <p> :mkdir <p> "
                ":rename <old> <new> :rm <p> :cpppair <p> :cppimpl [p] "
                ":cppcheck [on|off|next|prev] :rest [name|last] "
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
                ":unfoldall :incnum :decnum :lspinstall <server> :lspremove <server> :lspstatus "
                ":tsinstall <language> :tsstatus :tsreload "
                ":update [run] :reload :reloadconfig :reloadplugins "
                ":task [name] :tasknew <name> :taskrerun "
                ":term :termnew :termzoom :termfloat "
                ":textzoom [in|out|reset|1-4] "
                ":debug <program> :debugconfig [name] :debugstop "
                ":debugcontinue :debugnext :debugstep :debugout "
                ":gitstatus :gitdiff [file] :gitdiffstaged [file] "
                ":gitstage [file] :gitunstage [file] :gitstageall "
                ":gitunstageall :gitcommit <message> :gitlog :gitblame "
                ":gitrefresh :theme <name>"
                ":gitdiffclose"
                ":gitdiffrefresh");
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
      "  Ctrl+O           Jump back (jumplist)",
      "  Ctrl+I           Jump forward (jumplist; needs a terminal that reports Ctrl+I)",
      "  :jumplist        Pick a place from the jumplist",
      "  Ctrl+Shift+R     Rename the symbol under the cursor",
      "  Ctrl+Shift+F     Replace in selection, or search workspace",
      "  Ctrl+Shift+M     Diagnostics picker",
      "  Ctrl+Shift+O     Document symbol picker",
      "  Ctrl+Shift+W     Workspace symbol picker (across the project)",
      "  Alt+D / Alt+Y    Delete / yank an object: then i|a, then f|c|a|w|l",
      "  Alt+V            Selection: expand, shrink, carets, split, match all",
      "  Alt+Backspace    Delete the word the caret is on",
      "  Alt+X S / Alt+X B  Clear inside the string / the brackets",
      "  Alt+] / Alt+[    Next / previous function, class, diagnostic",
      "  Ctrl+Shift+X     Workspace diagnostics picker",
      "  :outline         Symbols/outline panel for the current file",
      "  Ctrl+T           Theme chooser",
      "  Ctrl+M           Toggle minimap",
      "  Ctrl+`           Open/focus/minimize terminal",
      "  :task [name]     Run local/global terminal task",
      "",
      "Modeless Editing",
      "  Type text         Insert at cursor",
      "  Esc               Clear selection or close active popup",
      "  Arrow keys        Move cursor",
      "  Shift+Arrows      Expand selection",
      "  Tab               Indent to next tab stop",
      "  Shift+Tab         Outdent line or selection",
      "  Ctrl+Enter        Insert line below",
      "  Ctrl+Shift+Enter  Insert line above",
      "",
      "Editing",
      "  Ctrl+Z / Ctrl+Y  Undo / Redo (Ctrl+Shift+Z also redoes)",
      "  Ctrl+A           Select all",
      "  Ctrl+C/X/V       Copy / Cut / Paste",
      "  Ctrl+D           Select next occurrence (multi-cursor)",
      "  Ctrl+Shift+D     Duplicate line",
      "  Ctrl+K           Delete line",
      "  Ctrl+/           Toggle comment",
      "  Ctrl+Backspace   Delete previous word",
      "  Ctrl+Shift+U     Uppercase selection/word",
      "  Ctrl+Shift+N     Lowercase selection/word",
      "  Ctrl+Space       LSP completion",
      "",
      "Tabs",
      "  Ctrl+Tab          Next tab in current pane (its own history)",
      "  Ctrl+Shift+Tab    Previous tab in current pane",
      "  Alt+, / Alt+.     Previous / next tab on the workspace strip",
      "  Alt+1..9 / Alt+0  Go to strip position 1..9 / the last tab",
      "  Alt+R then letter Jump to the tab showing that letter",
      "  Ctrl+click a tab  Close it (middle-click too)",
      "  Drag a tab        Reorder the workspace strip",
      "",
      "Pane & Layout (Ctrl+W, or press it alone for the menu)",
      "  Ctrl+W h/j/k/l     Focus pane left/down/up/right",
      "  Ctrl+W s / v       Split down / right",
      "  Ctrl+W Shift+S/V   Split up / left",
      "  Alt+Shift+Arrow    Split left/down/up/right (one chord)",
      "  Ctrl+W w / p       Next / previous pane",
      "  Ctrl+W c           Close current pane (Ctrl+Q too)",
      "  Ctrl+W o/x/e/r     Zoom / swap / equalize / rotate",
      "  Alt+H/J/K/L        Focus pane/explorer",
      "  Ctrl+Alt+Arrows    Focus pane",
      "  Ctrl+Shift+H/J/K/L Resize pane",
      "  Ctrl+Shift+Arrow   Resize pane",
      "  drag a divider     Resize that split (double-click resets it)",
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
      "  Alt+U / Alt+Shift+U  Uppercase / Lowercase",
      "  Alt+O              Sort selected lines",
      "  Alt+Up / Alt+Down  Move line up/down",
      "  Alt+I / Alt+A      Smart line start / line end",
      "  Alt+Shift+T        Floating terminal (Esc hides it)",
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
  for (size_t i = 0; i < lines.size(); i++)
  {
    out += lines[i];
    if (i + 1 < lines.size())
    {
      out.push_back('\n');
    }
  }
  show_popup(out, "Keybind Help");
}
