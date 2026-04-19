"""Palette command runtime state for jot_api."""


class PaletteRuntime:
    def __init__(self, internal_call_callback):
        self._entries = {}
        self._internal_call_callback = internal_call_callback

    def clear(self):
        self._entries.clear()

    def register(self, name, callback, completer=None, description=""):
        if not name:
            return
        key = str(name).strip().lower()
        if not key:
            return

        if isinstance(callback, str):
            callback_name = callback

            def _named_callback(arg=""):
                return self._internal_call_callback(callback_name, arg)

            callback_fn = _named_callback
        else:
            callback_fn = callback

        if not callable(callback_fn):
            return

        self._entries[key] = {
            "name": str(name).strip(),
            "callback": callback_fn,
            "completer": completer if callable(completer) else None,
            "description": description or "",
        }

    def register_completer(self, name, completer):
        if not name or not callable(completer):
            return
        key = str(name).strip().lower()
        entry = self._entries.get(key)
        if not entry:
            self._entries[key] = {
                "name": str(name).strip(),
                "callback": lambda _arg="": False,
                "completer": completer,
                "description": "",
            }
        else:
            entry["completer"] = completer

    def suggestions(self, seed):
        seed = (seed or "").strip()
        if seed.startswith(":"):
            seed = seed[1:].lstrip()

        if not self._entries:
            return []

        if not seed:
            names = [entry["name"] for entry in self._entries.values()]
            return sorted(set(names), key=lambda x: x.lower())

        cmd, _, remainder = seed.partition(" ")
        cmd = cmd.strip().lower()
        arg = remainder.strip()
        entering_arg = bool(remainder) or seed.endswith(" ")

        if not entering_arg:
            matches = []
            for key, entry in self._entries.items():
                if key.startswith(cmd):
                    matches.append(entry["name"])
            return sorted(set(matches), key=lambda x: x.lower())

        entry = self._entries.get(cmd)
        if not entry:
            return []

        completer = entry.get("completer")
        if not callable(completer):
            return []
        try:
            values = completer(arg)
        except TypeError:
            values = completer()
        except Exception:
            return []

        if values is None:
            return []
        if isinstance(values, str):
            values = [values]

        out = []
        for item in values:
            if item is None:
                continue
            text = str(item)
            if text:
                out.append(text)
        return out[:128]

    def execute(self, line, invoker):
        line = (line or "").strip()
        if not line:
            return False
        if line.startswith(":"):
            line = line[1:].lstrip()
        if not line:
            return False

        cmd, _, remainder = line.partition(" ")
        cmd_key = cmd.strip().lower()
        arg = remainder.strip()
        entry = self._entries.get(cmd_key)
        if not entry:
            return False

        callback = entry.get("callback")
        if not callable(callback):
            return False
        result = invoker(callback, arg)
        return bool(result is None or result)
