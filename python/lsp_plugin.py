import os
import jcode_api
from lsp import LSPClient

# Map root_uri -> {lang -> client}
clients = {} 

def get_root_uri(filepath):
    return os.getcwd() # Simple approximation

def get_language(filepath):
    if filepath.endswith(".py"): return "python"
    if filepath.endswith(".ts") or filepath.endswith(".js"): return "typescript"
    if filepath.endswith(".cpp") or filepath.endswith(".c") or filepath.endswith(".h"): return "cpp"
    return None

def get_client(filepath):
    lang = get_language(filepath)
    if not lang: return None
    
    root = get_root_uri(filepath)
    
    if root not in clients: clients[root] = {}
    if lang in clients[root]: return clients[root][lang]
    
    # Start new client
    cmd = None
    if lang == "python":
        cmd = "python-lsp-server" # lsp.py handles venv lookup
    elif lang == "typescript":
        cmd = "typescript-language-server --stdio"
    elif lang == "cpp":
        cmd = "clangd"
        
    if cmd:
        try:
            client = LSPClient(cmd, root)
            clients[root][lang] = client
            jcode_api.show_message(f"Started {lang} LSP")
            return client
        except Exception as e:
            jcode_api.show_message(f"Failed to start LSP: {e}")
            pass
            
    return None

@jcode_api.on_buffer_open
def on_open(filepath):
    jcode_api.show_message(f"DEBUG: on_open {filepath}")
    client = get_client(filepath)
    if client:
        content = jcode_api.get_buffer_content()
        client.did_open(filepath, content)

import threading

# Global timer for debounce
change_timers = {}

@jcode_api.on_buffer_change
def on_change(filepath):
    # Debounce typing
    if filepath in change_timers:
        change_timers[filepath].cancel()
    
    t = threading.Timer(0.1, perform_change, [filepath])
    change_timers[filepath] = t
    t.start()

def perform_change(filepath):
    client = get_client(filepath)
    if client:
        # Optimization: Pass content if available, or fetch
        # The C++ hook passes empty string currently
        content = jcode_api.get_buffer_content()
        client.did_change(filepath, content)

@jcode_api.on_buffer_save
def on_save(filepath):
    # Could send didSave
    pass

import lsp_manager

def install_lsp_callback(lang):
    if lang:
        jcode_api.show_message(f"Installing {lang}...")
        try:
            lsp_manager.install_server(lang)
            jcode_api.show_message(f"Installed {lang}")
        except Exception as e:
            jcode_api.show_message(f"Error installing {lang}: {e}")

def remove_lsp_callback(lang):
    if lang:
        jcode_api.show_message(f"Removing {lang}...")
        try:
            lsp_manager.remove_server(lang)
            jcode_api.show_message(f"Removed {lang}")
        except Exception as e:
            jcode_api.show_message(f"Error removing {lang}: {e}")

def cmd_lsp_install():
    jcode_api.show_input("Install LSP for (python/typescript/clangd): ", install_lsp_callback)

def cmd_lsp_remove():
    jcode_api.show_input("Remove LSP for (python/typescript): ", remove_lsp_callback)

jcode_api.register_command("LspInstall", cmd_lsp_install)
jcode_api.register_command("LspRemove", cmd_lsp_remove)
