import jcode_api
import os
import glob
import importlib.util

THEME_DIR = os.path.expanduser("~/.config/jcode/themes")

def load_theme(theme_path):
    # Load and execute the python file module
    spec = importlib.util.spec_from_file_location("dynamic_theme", theme_path)
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
        # jcode_api.show_message(f"Loaded theme: {os.path.basename(theme_path)}")
    except Exception as e:
        jcode_api.show_message(f"Error loading theme: {e}")

def get_available_themes():
    if not os.path.exists(THEME_DIR):
        return []
    files = glob.glob(os.path.join(THEME_DIR, "*.py"))
    themes = [os.path.splitext(os.path.basename(f))[0] for f in files]
    return [t for t in themes if t != "__init__"]

def theme_input_callback(choice):
    if not choice: return
    theme_name = choice.strip()
    path = os.path.join(THEME_DIR, f"{theme_name}.py")
    if os.path.exists(path):
        load_theme(path)
    else:
        jcode_api.show_message(f"Theme '{theme_name}' not found")

def cmd_switch_color_scheme():
    themes = get_available_themes()
    if not themes:
        jcode_api.show_message("No themes found in ~/.config/jcode/themes/")
        return
    
    prompt = "Select Color Scheme (" + "/".join(themes) + "): "
    jcode_api.show_input(prompt, theme_input_callback)

jcode_api.register_command("SwitchColorScheme", cmd_switch_color_scheme)
