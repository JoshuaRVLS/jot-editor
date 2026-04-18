import jot_api


def get_available_themes():
    return jot_api.list_colorschemes()


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
