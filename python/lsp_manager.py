import os
import shutil
import subprocess
import sys
import threading

from jot_api import Editor, config_path


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


def install_server(name):
    name = (name or "").strip().lower()
    if name == "python":
        threading.Thread(target=_install_python, daemon=True).start()
    elif name in ("typescript", "ts", "javascript", "js"):
        threading.Thread(
            target=_install_npm,
            args=("typescript typescript-language-server", "TypeScript LSP"),
            daemon=True,
        ).start()
    elif name in ("clangd", "cpp", "c++", "c"):
        if shutil.which("clangd"):
            Editor.show_message("clangd is already installed")
        else:
            Editor.show_message("Install clangd with your system package manager")
    else:
        Editor.show_message(f"Unknown LSP server: {name}")


def remove_server(name):
    name = (name or "").strip().lower()
    if name == "python":
        venv_path = config_path("venv")
        Editor.show_message("Removing Python LSP...")
        if os.path.exists(venv_path):
            shutil.rmtree(venv_path)
            Editor.show_message("Python LSP removed")
        else:
            Editor.show_message("Python LSP is not installed")
    elif name in ("typescript", "ts", "javascript", "js"):
        threading.Thread(
            target=_remove_npm,
            args=("typescript typescript-language-server", "TypeScript LSP"),
            daemon=True,
        ).start()
    elif name in ("clangd", "cpp", "c++", "c"):
        Editor.show_message("Remove clangd with your system package manager")
    else:
        Editor.show_message(f"Unknown LSP server: {name}")
