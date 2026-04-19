import os
import threading

import jot_api
import lsp_manager
from lsp import LSPClient


SERVER_CONFIG = {
    "python": {
        "command": "python-lsp-server",
        "aliases": {"python", "py"},
        "markers": [
            "pyproject.toml",
            "setup.py",
            "setup.cfg",
            "requirements.txt",
            ".git",
        ],
    },
    "typescript": {
        "command": "typescript-language-server --stdio",
        "aliases": {"typescript", "ts", "javascript", "js"},
        "markers": ["package.json", "tsconfig.json", "jsconfig.json", ".git"],
    },
    "cpp": {
        "command": "clangd",
        "aliases": {"cpp", "c++", "c", "clangd"},
        "markers": [
            "compile_commands.json",
            "compile_flags.txt",
            "CMakeLists.txt",
            ".clangd",
            ".git",
        ],
    },
    "go": {
        "command": "gopls",
        "aliases": {"go", "golang", "gopls"},
        "markers": ["go.mod", "go.work", ".git"],
    },
    "rust": {
        "command": "rust-analyzer",
        "aliases": {"rust", "rs", "rust-analyzer"},
        "markers": ["Cargo.toml", "rust-project.json", ".git"],
    },
    "bash": {
        "command": "bash-language-server start",
        "aliases": {"bash", "sh", "shell"},
        "markers": [".git", ".editorconfig"],
    },
    "html": {
        "command": "vscode-html-language-server --stdio",
        "aliases": {"html", "htm"},
        "markers": ["package.json", ".git"],
    },
    "css": {
        "command": "vscode-css-language-server --stdio",
        "aliases": {"css", "scss", "less"},
        "markers": ["package.json", ".git"],
    },
    "json": {
        "command": "vscode-json-language-server --stdio",
        "aliases": {"json"},
        "markers": ["package.json", ".git"],
    },
    "yaml": {
        "command": "yaml-language-server --stdio",
        "aliases": {"yaml", "yml"},
        "markers": ["package.json", ".git"],
    },
}

EXTENSION_TO_LANG = {
    ".py": "python",
    ".ts": "typescript",
    ".tsx": "typescript",
    ".js": "typescript",
    ".jsx": "typescript",
    ".c": "cpp",
    ".cc": "cpp",
    ".cpp": "cpp",
    ".cxx": "cpp",
    ".h": "cpp",
    ".hh": "cpp",
    ".hpp": "cpp",
    ".go": "go",
    ".rs": "rust",
    ".sh": "bash",
    ".bash": "bash",
    ".zsh": "bash",
    ".html": "html",
    ".htm": "html",
    ".css": "css",
    ".scss": "css",
    ".less": "css",
    ".json": "json",
    ".yaml": "yaml",
    ".yml": "yaml",
}

clients = {}
change_timers = {}

_manager_state = {
    "visible": False,
    "mode": "main",  # main|install|remove
    "index": 0,
    "offset": 0,
}


def _normalize_lang_name(name):
    lower = (name or "").strip().lower()
    for lang, config in SERVER_CONFIG.items():
        if lower == lang or lower in config["aliases"]:
            return lang
    return None


def _detect_language(filepath):
    _, ext = os.path.splitext(filepath.lower())
    return EXTENSION_TO_LANG.get(ext)


def _find_workspace_root(filepath, markers):
    current = os.path.abspath(os.path.dirname(filepath))
    last = None
    while current and current != last:
        for marker in markers:
            if os.path.exists(os.path.join(current, marker)):
                return current
        last = current
        current = os.path.dirname(current)
    return os.getcwd()


def _get_client_key(filepath):
    lang = _detect_language(filepath)
    if not lang:
        return None, None
    root = _find_workspace_root(filepath, SERVER_CONFIG[lang]["markers"])
    return lang, root


def _client_id(lang, root):
    return f"{lang}:{os.path.abspath(root)}"


def _summarize_client(client):
    open_count = len(client.open_files)
    state = "ready" if client.is_initialized and client.is_running() else "starting"
    return f"{client.server_name} [{state}] {client.root_path} ({open_count} open)"


def get_client(filepath, announce=False):
    lang, root = _get_client_key(filepath)
    if not lang:
        return None

    key = _client_id(lang, root)
    existing = clients.get(key)
    if existing and existing.is_running():
        return existing

    command = SERVER_CONFIG[lang]["command"]
    try:
        client = LSPClient(command, root, lang, workspace_name=os.path.basename(root))
        clients[key] = client
        if announce:
            jot_api.show_message(f"LSP: started {lang} for {os.path.basename(root) or root}")
        return client
    except Exception as exc:
        jot_api.show_message(f"LSP start failed for {lang}: {exc}")
        return None


@jot_api.on_buffer_open
def on_open(filepath):
    client = get_client(filepath)
    if client:
        client.did_open(filepath, jot_api.get_buffer_content())


def _send_change(filepath):
    client = get_client(filepath)
    if client:
        client.did_change(filepath, jot_api.get_buffer_content())


@jot_api.on_buffer_change
def on_change(filepath):
    timer = change_timers.get(filepath)
    if timer:
        timer.cancel()

    timer = threading.Timer(0.15, _send_change, [filepath])
    timer.daemon = True
    change_timers[filepath] = timer
    timer.start()


@jot_api.on_buffer_save
def on_save(filepath):
    client = get_client(filepath)
    if client:
        client.did_save(filepath, jot_api.get_buffer_content())


def _stop_client(client):
    try:
        client.stop()
    except Exception:
        pass


def cmd_lsp_install():
    choices = ", ".join(lsp_manager.supported_servers())
    jot_api.show_input(
        f"Install LSP ({choices}): ",
        lambda value: lsp_manager.install_server(
            _normalize_lang_name(value) or lsp_manager.normalize_server_name(value) or value
        ),
    )


def cmd_lsp_remove():
    installed = lsp_manager.installed_servers()
    choices = ", ".join(installed) if installed else "none"
    jot_api.show_input(
        f"Remove LSP (installed: {choices}): ",
        lambda value: lsp_manager.remove_server(
            _normalize_lang_name(value) or lsp_manager.normalize_server_name(value) or value
        ),
    )


def cmd_lsp_status(_arg=""):
    live = []
    dead = []
    for key, client in list(clients.items()):
        if client.is_running():
            live.append(_summarize_client(client))
        else:
            dead.append(key)

    for key in dead:
        clients.pop(key, None)

    if not live:
        jot_api.show_message("LSP: no active servers")
        return

    jot_api.show_message("LSP: " + " | ".join(live[:3]))


def cmd_lsp_restart():
    reopen_queue = []
    for timer in change_timers.values():
        timer.cancel()
    change_timers.clear()

    restarted = 0
    for key, client in list(clients.items()):
        lang = client.server_name
        root = client.root_path
        tracked_files = list(client.open_files.keys())
        _stop_client(client)
        try:
            clients[key] = LSPClient(
                SERVER_CONFIG[lang]["command"],
                root,
                lang,
                workspace_name=os.path.basename(root),
            )
            reopen_queue.append((clients[key], tracked_files))
            restarted += 1
        except Exception as exc:
            clients.pop(key, None)
            jot_api.show_message(f"LSP restart failed for {lang}: {exc}")

    for client, tracked_files in reopen_queue:
        for path in tracked_files:
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    client.did_open(path, handle.read())
            except Exception:
                pass

    if restarted:
        jot_api.show_message(f"LSP: restarted {restarted} server(s)")
    else:
        jot_api.show_message("LSP: nothing to restart")


def cmd_lsp_stop():
    stopped = 0
    for _, client in list(clients.items()):
        _stop_client(client)
        stopped += 1
    clients.clear()
    for timer in change_timers.values():
        timer.cancel()
    change_timers.clear()
    jot_api.show_message(f"LSP: stopped {stopped} server(s)")


def _manager_install():
    _manager_state["mode"] = "install"
    _manager_state["index"] = 0
    _manager_state["offset"] = 0
    _manager_render()


def _manager_remove():
    _manager_state["mode"] = "remove"
    _manager_state["index"] = 0
    _manager_state["offset"] = 0
    _manager_render()


def _manager_list():
    jot_api.show_message(lsp_manager.servers_status_message())


def _manager_status():
    cmd_lsp_status()


def _manager_restart():
    cmd_lsp_restart()


def _manager_stop():
    cmd_lsp_stop()


def _manager_help():
    _manager_state["visible"] = True
    _manager_state["mode"] = "main"
    _manager_state["index"] = 0
    _manager_state["offset"] = 0
    _manager_render()


def _manager_select(value):
    raw = (value or "").strip().lower()
    actions = {
        "1": _manager_list,
        "list": _manager_list,
        "ls": _manager_list,
        "2": _manager_install,
        "install": _manager_install,
        "add": _manager_install,
        "3": _manager_remove,
        "remove": _manager_remove,
        "rm": _manager_remove,
        "4": _manager_status,
        "status": _manager_status,
        "5": _manager_restart,
        "restart": _manager_restart,
        "6": _manager_stop,
        "stop": _manager_stop,
        "help": _manager_help,
        "h": _manager_help,
    }
    fn = actions.get(raw)
    if not fn:
        _manager_render()
        return
    fn()


def cmd_lsp_manager(_arg=""):
    _manager_help()


def _manager_main_items():
    return [
        ("Server List", _manager_list),
        ("Install Server", _manager_install),
        ("Remove Server", _manager_remove),
        ("Active LSP Status", _manager_status),
        ("Restart Active LSP", _manager_restart),
        ("Stop Active LSP", _manager_stop),
        ("Close", _manager_close),
    ]


def _manager_install_items():
    items = []
    for name in lsp_manager.supported_servers():
        installed = lsp_manager.is_server_installed(name)
        tag = "installed" if installed else "not installed"
        items.append((f"{name} [{tag}]", lambda _n=name: lsp_manager.install_server(_n)))
    items.append(("Back", _manager_back_to_main))
    return items


def _manager_remove_items():
    installed = lsp_manager.installed_servers()
    items = [(f"{name} [installed]", lambda _n=name: lsp_manager.remove_server(_n)) for name in installed]
    if not items:
        items.append(("No installed servers", lambda: None))
    items.append(("Back", _manager_back_to_main))
    return items


def _manager_items():
    mode = _manager_state["mode"]
    if mode == "install":
        return _manager_install_items()
    if mode == "remove":
        return _manager_remove_items()
    return _manager_main_items()


def _manager_title():
    mode = _manager_state["mode"]
    if mode == "install":
        return "LSP Manager - Install"
    if mode == "remove":
        return "LSP Manager - Remove"
    return "LSP Manager"


def _manager_close():
    _manager_state["visible"] = False
    jot_api.hide_popup()
    return True


def _manager_back_to_main():
    _manager_state["mode"] = "main"
    _manager_state["index"] = 0
    _manager_state["offset"] = 0
    _manager_render()


def _manager_render():
    if not _manager_state["visible"]:
        return

    items = _manager_items()
    if not items:
        items = [("No items", lambda: None)]

    idx = max(0, min(_manager_state["index"], len(items) - 1))
    _manager_state["index"] = idx

    max_rows = 12
    offset = _manager_state["offset"]
    if idx < offset:
        offset = idx
    if idx >= offset + max_rows:
        offset = idx - max_rows + 1
    offset = max(0, min(offset, max(0, len(items) - max_rows)))
    _manager_state["offset"] = offset

    lines = [_manager_title(), "Up/Down: Move  Enter: Select  Esc: Close", ""]
    for i in range(offset, min(len(items), offset + max_rows)):
        prefix = "> " if i == idx else "  "
        lines.append(prefix + items[i][0])

    if _manager_state["mode"] == "main":
        lines.append("")
        lines.append(lsp_manager.servers_status_message(max_items=4))

    jot_api.show_popup("\n".join(lines), 4, 3)


def _manager_move(delta):
    if not _manager_state["visible"]:
        return False
    items = _manager_items()
    if not items:
        return True
    size = len(items)
    _manager_state["index"] = (_manager_state["index"] + delta + size) % size
    _manager_render()
    return True


def _manager_activate():
    if not _manager_state["visible"]:
        return False
    items = _manager_items()
    if not items:
        return True
    _, action = items[_manager_state["index"]]
    try:
        action()
    except Exception as exc:
        jot_api.show_message(f"LSP Manager action failed: {exc}")
    _manager_render()
    return True


def _manager_cancel():
    if not _manager_state["visible"]:
        return False
    _manager_close()
    return True


@jot_api.on_keybind("up", "all")
def _manager_key_up(_arg=""):
    return _manager_move(-1)


@jot_api.on_keybind("down", "all")
def _manager_key_down(_arg=""):
    return _manager_move(1)


@jot_api.on_keybind("enter", "all")
def _manager_key_enter(_arg=""):
    return _manager_activate()


@jot_api.on_keybind("esc", "all")
def _manager_key_esc(_arg=""):
    return _manager_cancel()


jot_api.register_command("LspInstall", cmd_lsp_install)
jot_api.register_command("LspRemove", cmd_lsp_remove)
jot_api.register_command("LspStatus", cmd_lsp_status)
jot_api.register_command("LspRestart", cmd_lsp_restart)
jot_api.register_command("LspStop", cmd_lsp_stop)
jot_api.register_command("LspManager", cmd_lsp_manager)
