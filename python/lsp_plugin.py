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
}

clients = {}
change_timers = {}


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
    jot_api.show_input(
        "Install LSP for (python/typescript/clangd): ",
        lambda value: lsp_manager.install_server(_normalize_lang_name(value) or value),
    )


def cmd_lsp_remove():
    jot_api.show_input(
        "Remove LSP for (python/typescript/clangd): ",
        lambda value: lsp_manager.remove_server(_normalize_lang_name(value) or value),
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


jot_api.register_command("LspInstall", cmd_lsp_install)
jot_api.register_command("LspRemove", cmd_lsp_remove)
jot_api.register_command("LspStatus", cmd_lsp_status)
jot_api.register_command("LspRestart", cmd_lsp_restart)
jot_api.register_command("LspStop", cmd_lsp_stop)
