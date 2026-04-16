import os
from pathlib import Path

import jcode_api

THEME_DIRS = [
    Path(jcode_api.colors_path()),
    Path(os.path.expanduser("~/.config/jcode/themes")),
]


def get_available_themes():
    names = set()
    for directory in THEME_DIRS:
        if not directory.exists():
            continue
        for file in directory.glob("*.py"):
            if file.name != "__init__.py":
                names.add(file.stem)
    return sorted(names)


def theme_input_callback(choice):
    if not choice:
        return
    jcode_api.apply_colorscheme(choice.strip())


def cmd_switch_color_scheme():
    themes = get_available_themes()
    if not themes:
        jcode_api.show_message("No themes found in ~/.config/jcode/configs/colors/")
        return

    prompt = "colorscheme " + "/".join(themes) + ": "
    jcode_api.show_input(prompt, theme_input_callback)


jcode_api.register_command("SwitchColorScheme", cmd_switch_color_scheme)
jcode_api.register_command("Colorscheme", cmd_switch_color_scheme)
