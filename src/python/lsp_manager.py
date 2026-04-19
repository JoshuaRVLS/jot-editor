import os
import shutil
import subprocess
import sys
import threading

from jot_api import Editor, config_path


SERVER_DEFINITIONS = {
    "python": {
        "aliases": {"python", "py", "pylsp"},
        "check": lambda: os.path.exists(_venv_bin("pylsp")) or shutil.which("pylsp"),
        "installer": lambda: _install_python(),
        "remover": lambda: _remove_python(),
        "display": "Python (pylsp)",
    },
    "typescript": {
        "aliases": {"typescript", "ts", "javascript", "js"},
        "check": lambda: shutil.which("typescript-language-server"),
        "installer": lambda: _install_npm(
            "typescript typescript-language-server", "TypeScript/JavaScript LSP"
        ),
        "remover": lambda: _remove_npm(
            "typescript typescript-language-server", "TypeScript/JavaScript LSP"
        ),
        "display": "TypeScript/JavaScript",
    },
    "cpp": {
        "aliases": {"cpp", "c++", "c", "clangd"},
        "check": lambda: shutil.which("clangd"),
        "installer": lambda: _manual_install(
            "clangd", "Install clangd with your system package manager"
        ),
        "remover": lambda: _manual_remove("clangd"),
        "display": "C/C++ (clangd)",
    },
    "go": {
        "aliases": {"go", "golang", "gopls"},
        "check": lambda: shutil.which("gopls"),
        "installer": lambda: _install_go_tool("golang.org/x/tools/gopls@latest", "gopls"),
        "remover": lambda: _manual_remove("gopls"),
        "display": "Go (gopls)",
    },
    "rust": {
        "aliases": {"rust", "rust-analyzer", "ra"},
        "check": lambda: shutil.which("rust-analyzer"),
        "installer": lambda: _install_rust_analyzer(),
        "remover": lambda: _manual_remove("rust-analyzer"),
        "display": "Rust (rust-analyzer)",
    },
    "bash": {
        "aliases": {"bash", "sh", "shell"},
        "check": lambda: shutil.which("bash-language-server"),
        "installer": lambda: _install_npm("bash-language-server", "Bash LSP"),
        "remover": lambda: _remove_npm("bash-language-server", "Bash LSP"),
        "display": "Bash/Shell",
    },
    "web": {
        "aliases": {"web", "html", "css", "json"},
        "check": lambda: shutil.which("vscode-html-language-server")
        and shutil.which("vscode-css-language-server")
        and shutil.which("vscode-json-language-server"),
        "installer": lambda: _install_npm("vscode-langservers-extracted", "Web LSP bundle"),
        "remover": lambda: _remove_npm("vscode-langservers-extracted", "Web LSP bundle"),
        "display": "HTML/CSS/JSON",
    },
    "yaml": {
        "aliases": {"yaml", "yml"},
        "check": lambda: shutil.which("yaml-language-server"),
        "installer": lambda: _install_npm("yaml-language-server", "YAML LSP"),
        "remover": lambda: _remove_npm("yaml-language-server", "YAML LSP"),
        "display": "YAML",
    },
}


def _run_silent(cmd):
    try:
        subprocess.check_call(
            cmd,
            shell=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return True
    except subprocess.CalledProcessError:
        return False


def _venv_bin(name):
    return os.path.join(config_path("venv"), "bin", name)


def _install_python():
    Editor.show_message("Installing Python LSP...")
    venv_path = config_path("venv")

    try:
        if not os.path.exists(venv_path):
            os.makedirs(os.path.dirname(venv_path), exist_ok=True)
            if not _run_silent(f"{sys.executable} -m venv {venv_path}"):
                Editor.show_message("Failed to create Python LSP venv")
                return

        pip_path = _venv_bin("pip")
        if not os.path.exists(pip_path):
            Editor.show_message("pip not found inside Python LSP venv")
            return

        if _run_silent(f"{pip_path} install -U pip python-lsp-server"):
            Editor.show_message(f"Python LSP ready: {_venv_bin('pylsp')}")
        else:
            Editor.show_message("Failed to install python-lsp-server")
    except Exception as exc:
        Editor.show_message(f"Python LSP install error: {exc}")


def _remove_python():
    venv_path = config_path("venv")
    Editor.show_message("Removing Python LSP...")
    if os.path.exists(venv_path):
        shutil.rmtree(venv_path)
        Editor.show_message("Python LSP removed")
    else:
        Editor.show_message("Python LSP is not installed")


def _install_npm(packages, display_name):
    Editor.show_message(f"Installing {display_name}...")
    if shutil.which("npm") is None:
        Editor.show_message("npm not found")
        return

    if _run_silent(f"npm install -g {packages}"):
        Editor.show_message(f"{display_name} installed")
    else:
        Editor.show_message(f"Failed to install {display_name}")


def _remove_npm(packages, display_name):
    Editor.show_message(f"Removing {display_name}...")
    if shutil.which("npm") is None:
        Editor.show_message("npm not found")
        return

    if _run_silent(f"npm uninstall -g {packages}"):
        Editor.show_message(f"{display_name} removed")
    else:
        Editor.show_message(f"Failed to remove {display_name}")


def _install_go_tool(module_name, display_name):
    Editor.show_message(f"Installing {display_name}...")
    if shutil.which("go") is None:
        Editor.show_message("go not found")
        return
    if _run_silent(f"go install {module_name}"):
        Editor.show_message(f"{display_name} installed (ensure GOPATH/bin in PATH)")
    else:
        Editor.show_message(f"Failed to install {display_name}")


def _install_rust_analyzer():
    Editor.show_message("Installing rust-analyzer...")
    if shutil.which("rustup"):
        if _run_silent("rustup component add rust-analyzer"):
            Editor.show_message("rust-analyzer installed")
            return
    Editor.show_message("Install rust-analyzer with rustup or your package manager")


def _manual_install(binary_name, hint):
    if shutil.which(binary_name):
        Editor.show_message(f"{binary_name} is already installed")
    else:
        Editor.show_message(hint)


def _manual_remove(binary_name):
    Editor.show_message(f"Remove {binary_name} with your system package manager")


def normalize_server_name(name):
    lower = (name or "").strip().lower()
    if not lower:
        return None
    for key, cfg in SERVER_DEFINITIONS.items():
        if lower == key or lower in cfg["aliases"]:
            return key
    return None


def supported_servers():
    return sorted(SERVER_DEFINITIONS.keys())


def is_server_installed(name):
    key = normalize_server_name(name)
    if not key:
        return False
    checker = SERVER_DEFINITIONS[key]["check"]
    try:
        return bool(checker())
    except Exception:
        return False


def installed_servers():
    out = []
    for key in supported_servers():
        if is_server_installed(key):
            out.append(key)
    return out


def servers_status_lines():
    lines = []
    for key in supported_servers():
        cfg = SERVER_DEFINITIONS[key]
        installed = is_server_installed(key)
        mark = "OK" if installed else "--"
        lines.append(f"{key:<10} [{mark}] {cfg['display']}")
    return lines


def servers_status_message(max_items=6):
    lines = servers_status_lines()
    if not lines:
        return "LSP Manager: no servers configured"
    head = lines[:max_items]
    suffix = " ..." if len(lines) > max_items else ""
    return "LSP Servers: " + " | ".join(head) + suffix


def install_server(name):
    key = normalize_server_name(name)
    if not key:
        Editor.show_message(f"Unknown LSP server: {name}")
        return

    target = SERVER_DEFINITIONS[key]["installer"]
    threading.Thread(target=target, daemon=True).start()


def remove_server(name):
    key = normalize_server_name(name)
    if not key:
        Editor.show_message(f"Unknown LSP server: {name}")
        return

    target = SERVER_DEFINITIONS[key]["remover"]
    threading.Thread(target=target, daemon=True).start()
