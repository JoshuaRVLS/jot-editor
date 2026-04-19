"""Theme/colorscheme runtime helpers for jot_api."""


class ThemeRuntime:
    def __init__(
        self,
        highlight_map,
        colors_dir,
        legacy_themes_dir,
        runtime_theme_dirs,
        set_theme_color,
        show_message,
    ):
        self._highlight_map = highlight_map
        self._colors_dir = colors_dir
        self._legacy_themes_dir = legacy_themes_dir
        self._runtime_theme_dirs = runtime_theme_dirs
        self._set_theme_color = set_theme_color
        self._show_message = show_message

    @staticmethod
    def _normalize_theme_value(value):
        if isinstance(value, str):
            lookup = {
                "none": -1,
                "default": -1,
                "fg": -1,
                "bg": -1,
            }
            return lookup.get(value.lower(), -1)
        if value is None:
            return -1
        return int(value)

    def set_hl(self, group, spec):
        slot = self._highlight_map.get(group, group)
        fg = self._normalize_theme_value(spec.get("fg"))
        bg = self._normalize_theme_value(spec.get("bg"))
        self._set_theme_color(slot, fg, bg)

    def list_colorschemes(self):
        names = set()
        for directory in (
            self._colors_dir,
            self._legacy_themes_dir,
            *self._runtime_theme_dirs,
        ):
            if not directory.exists():
                continue
            for file in directory.glob("*.py"):
                if file.name == "__init__.py":
                    continue
                names.add(file.stem)
        return sorted(names)

    def apply_colorscheme(self, name, api_module):
        candidates = [
            self._colors_dir / f"{name}.py",
            self._legacy_themes_dir / f"{name}.py",
        ]
        for runtime_dir in self._runtime_theme_dirs:
            candidates.append(runtime_dir / f"{name}.py")

        for candidate in candidates:
            if candidate.exists():
                namespace = {"__file__": str(candidate), "__name__": "__main__"}
                with open(candidate, "r", encoding="utf-8") as handle:
                    code = compile(handle.read(), str(candidate), "exec")
                    exec(code, namespace, namespace)

                if callable(namespace.get("setup")):
                    namespace["setup"](api_module)
                elif callable(namespace.get("apply")):
                    namespace["apply"]()
                elif isinstance(namespace.get("theme"), dict):
                    for group, hl in namespace["theme"].items():
                        self.set_hl(group, hl)

                return True

        self._show_message(f"Colorscheme '{name}' not found")
        return False
