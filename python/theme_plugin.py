import os
from pathlib import Path

import jot_api

THEME_DIRS = [
    Path(jot_api.colors_path()),
    Path(os.path.expanduser("~/.config/jot/themes")),
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
    jot_api.apply_colorscheme(choice.strip())


def cmd_switch_color_scheme():
    themes = get_available_themes()
    if not themes:
        jot_api.show_message("No themes found in ~/.config/jot/configs/colors/")
        return

    prompt = "colorscheme " + "/".join(themes) + ": "
    jot_api.show_input(prompt, theme_input_callback)


jot_api.register_command("SwitchColorScheme", cmd_switch_color_scheme)
jot_api.register_command("Colorscheme", cmd_switch_color_scheme)
