"""General editor command palette bindings."""

from pathlib import Path


_FORWARDED_EX_COMMANDS = (
    "new",
    "enew",
    "bd",
    "bdelete",
    "close",
    "find",
    "ff",
    "mkfile",
    "mkdir",
    "rename",
    "rm",
    "lspstart",
    "lspmanager",
    "lspstatus",
    "lspstop",
    "lsprestart",
    "gitrefresh",
    "gitstatus",
    "gitdiff",
    "gitblame",
    "recent",
    "openrecent",
    "reopen",
    "reopenlast",
    "autosave",
    "search",
    "format",
    "trim",
    "line",
    "goto",
    "resizeleft",
    "resizeright",
    "resizeup",
    "resizedown",
    "help",
    "h",
)


def register_editor_commands(api):
    register_palette_command = api["register_palette_command"]
    register_command_completer = api["register_command_completer"]
    show_message = api["show_message"]
    list_colorschemes = api["list_colorschemes"]
    open_file = api["open_file"]
    save_file = api["save_file"]
    split_pane = api["split_pane"]
    focus_next_pane = api["focus_next_pane"]
    focus_prev_pane = api["focus_prev_pane"]
    toggle_terminal = api["toggle_terminal"]
    toggle_sidebar = api["toggle_sidebar"]
    request_quit = api["request_quit"]
    save_and_quit = api["save_and_quit"]
    toggle_minimap = api["toggle_minimap"]
    apply_colorscheme = api["apply_colorscheme"]
    execute_ex_command = api["execute_ex_command"]

    def _complete_files(prefix=""):
        raw = (prefix or "").strip()
        base = Path.cwd()
        if raw:
            candidate = Path(raw).expanduser()
            parent = candidate.parent if str(candidate.parent) != "" else Path(".")
            if not parent.is_absolute():
                parent = (base / parent).resolve()
            needle = candidate.name.lower()
        else:
            parent = base
            needle = ""

        if not parent.exists() or not parent.is_dir():
            return []

        out = []
        for item in sorted(parent.iterdir(), key=lambda p: p.name.lower()):
            name = item.name
            if needle and not name.lower().startswith(needle):
                continue
            shown = str((Path(raw).parent / name) if raw else Path(name))
            if item.is_dir():
                shown += "/"
            out.append(shown)
            if len(out) >= 128:
                break
        return out

    def _complete_themes(prefix=""):
        needle = (prefix or "").strip().lower()
        out = []
        for theme in list_colorschemes():
            if not needle or theme.lower().startswith(needle):
                out.append(theme)
        return out[:128]

    def _cmd_open(arg=""):
        path = (arg or "").strip()
        if not path:
            return False
        open_file(path)
        return True

    def _cmd_save(_arg=""):
        save_file()
        return True

    def _cmd_split_h(_arg=""):
        split_pane("horizontal")
        return True

    def _cmd_split_v(_arg=""):
        split_pane("vertical")
        return True

    def _cmd_next_pane(_arg=""):
        focus_next_pane()
        return True

    def _cmd_prev_pane(_arg=""):
        focus_prev_pane()
        return True

    def _cmd_term(_arg=""):
        toggle_terminal()
        return True

    def _cmd_sidebar(_arg=""):
        toggle_sidebar()
        return True

    def _cmd_quit(_arg=""):
        request_quit(False)
        return True

    def _cmd_quit_force(_arg=""):
        request_quit(True)
        return True

    def _cmd_save_quit(_arg=""):
        save_and_quit()
        return True

    def _cmd_minimap(_arg=""):
        toggle_minimap()
        return True

    def _cmd_theme(arg=""):
        name = (arg or "").strip()
        if not name:
            themes = list_colorschemes()
            if themes:
                show_message("Themes: " + ", ".join(themes[:12]))
            else:
                show_message("No themes found")
            return True
        return bool(apply_colorscheme(name))

    def _forward_builtin_ex(name):
        def _wrapped(arg=""):
            arg = (arg or "").strip()
            line = f"{name} {arg}".strip()
            execute_ex_command(line)
            return True

        return _wrapped

    register_palette_command("e", _cmd_open, completer=_complete_files)
    register_palette_command("edit", _cmd_open, completer=_complete_files)
    register_palette_command("open", _cmd_open, completer=_complete_files)
    register_palette_command("w", _cmd_save)
    register_palette_command("write", _cmd_save)
    register_palette_command("sp", _cmd_split_h)
    register_palette_command("split", _cmd_split_h)
    register_palette_command("splith", _cmd_split_h)
    register_palette_command("vsp", _cmd_split_v)
    register_palette_command("splitv", _cmd_split_v)
    register_palette_command("bn", _cmd_next_pane)
    register_palette_command("nextpane", _cmd_next_pane)
    register_palette_command("bp", _cmd_prev_pane)
    register_palette_command("prevpane", _cmd_prev_pane)
    register_palette_command("term", _cmd_term)
    register_palette_command("terminal", _cmd_term)
    register_palette_command("sidebar", _cmd_sidebar)
    register_palette_command("q", _cmd_quit)
    register_palette_command("quit", _cmd_quit)
    register_palette_command("q!", _cmd_quit_force)
    register_palette_command("quit!", _cmd_quit_force)
    register_palette_command("wq", _cmd_save_quit)
    register_palette_command("x", _cmd_save_quit)
    register_palette_command("xit", _cmd_save_quit)
    register_palette_command("minimap", _cmd_minimap)
    register_palette_command("theme", _cmd_theme, completer=_complete_themes)
    register_palette_command("colorscheme", _cmd_theme, completer=_complete_themes)
    register_palette_command("colo", _cmd_theme, completer=_complete_themes)

    for cmd in _FORWARDED_EX_COMMANDS:
        register_palette_command(cmd, _forward_builtin_ex(cmd))

    register_command_completer("find", _complete_files)
    register_command_completer("ff", _complete_files)
    register_command_completer("mkfile", _complete_files)
    register_command_completer("mkdir", _complete_files)
    register_command_completer("rm", _complete_files)
    register_command_completer(
        "openrecent", lambda _arg="": [str(i + 1) for i in range(12)]
    )
