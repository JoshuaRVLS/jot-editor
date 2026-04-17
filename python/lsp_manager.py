import subprocess
import threading
import sys
import shutil
import os
from jot_api import Editor, config_path

def _run_silent(cmd):
    try:
        subprocess.check_call(cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return True
    except subprocess.CalledProcessError:
        return False

def _install_python():
    Editor.show_message("Installing Python LSP (background)...")
    venv_path = config_path("venv")
    
    try:
        # 1. Create venv
        if not os.path.exists(venv_path):
             # Ensure directory exists for venv creation
             parent = os.path.dirname(venv_path)
             if not os.path.exists(parent):
                 os.makedirs(parent)

             if not _run_silent(f"{sys.executable} -m venv {venv_path}"):
                 Editor.show_message("Failed to create venv.")
                 return

        pip_path = os.path.join(venv_path, "bin", "pip")
        if not os.path.exists(pip_path):
             Editor.show_message("pip not found in venv.")
             return
        
        # 2. Install pylsp
        if _run_silent(f"{pip_path} install python-lsp-server"):
             pylsp = os.path.join(venv_path, "bin", "pylsp")
             Editor.show_message(f"Python LSP installed! ({pylsp})")
        else:
             Editor.show_message("Failed to install python-lsp-server.")

    except Exception as e:
        Editor.show_message(f"Install error: {e}")

def _install_npm(pkg_name, display_name):
    Editor.show_message(f"Installing {display_name} (background)...")
    if shutil.which("npm") is None:
        Editor.show_message("Error: npm not found.")
        return

    # Use global install for npm as originally requested, or handle silent
    if _run_silent(f"npm install -g {pkg_name}"):
        Editor.show_message(f"{display_name} installed successfully!")
    else:
        Editor.show_message(f"Failed to install {display_name}. (Check permissions?)")

def install_server(name):
    if name == "python":
        threading.Thread(target=_install_python).start()
    elif name == "typescript":
        # Install typescript and typescript-language-server
        threading.Thread(target=_install_npm, args=("typescript typescript-language-server", "TypeScript LSP")).start()
    elif name == "clangd":
        if shutil.which("clangd"):
             Editor.show_message("clangd is already installed.")
        else:
             Editor.show_message("Managed install for clangd not supported. Please use system package manager.")
    else:
        Editor.show_message(f"Unknown server: {name}")

def remove_server(name):
    if name == "python":
        Editor.show_message("Removing Python LSP...")
        venv_path = config_path("venv")
        if os.path.exists(venv_path):
            shutil.rmtree(venv_path)
            Editor.show_message("Python LSP (venv) removed.")
    elif name == "typescript":
        threading.Thread(target=_install_npm, args=("typescript-language-server", "TypeScript LSP (Uninstalling - wait, logic error)")).start()
        # npm uninstall logic needs separate func or flag
        Editor.show_message("Use npm uninstall -g typescript-language-server manually for now to avoid blocking.")
