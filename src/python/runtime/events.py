"""Event/autocmd runtime helpers for jot_api."""

import fnmatch
import json
from collections import defaultdict


class EventsRuntime:
    def __init__(self, safe_call):
        self._safe_call = safe_call
        self._event_callbacks = defaultdict(list)
        self._buffer_open_callbacks = []
        self._buffer_change_callbacks = []
        self._buffer_save_callbacks = []

    def clear(self):
        self._event_callbacks.clear()
        self._buffer_open_callbacks.clear()
        self._buffer_change_callbacks.clear()
        self._buffer_save_callbacks.clear()

    def autocmd(self, event, pattern="*", group=None):
        event_name = event.lower()

        def decorator(func):
            self._event_callbacks[event_name].append(
                {"callback": func, "pattern": pattern or "*", "group": group}
            )
            return func

        return decorator

    def on_startup(self, callback):
        @self.autocmd("startup")
        def _wrapped(_event):
            return callback()

        return callback

    @staticmethod
    def _matches_event_pattern(pattern, payload):
        if not pattern or pattern == "*":
            return True
        candidate = (
            payload.get("filepath")
            or payload.get("command")
            or payload.get("name")
            or ""
        )
        return fnmatch.fnmatch(candidate, pattern)

    def emit(self, event, payload=None):
        event_name = (event or "").lower()
        if isinstance(payload, str):
            try:
                payload = json.loads(payload or "{}")
            except json.JSONDecodeError:
                payload = {}
        payload = payload or {}
        payload.setdefault("event", event_name)

        for entry in list(self._event_callbacks.get(event_name, [])):
            if self._matches_event_pattern(entry["pattern"], payload):
                self._safe_call(entry["callback"], payload)

    def on_buffer_open(self, callback):
        self._buffer_open_callbacks.append(callback)
        return callback

    def on_buffer_change(self, callback):
        self._buffer_change_callbacks.append(callback)
        return callback

    def on_buffer_save(self, callback):
        self._buffer_save_callbacks.append(callback)
        return callback

    def dispatch_buffer_open(self, filepath):
        for cb in list(self._buffer_open_callbacks):
            self._safe_call(cb, filepath)
        self.emit("buffer_open", {"filepath": filepath})

    def dispatch_buffer_change(self, filepath):
        for cb in list(self._buffer_change_callbacks):
            self._safe_call(cb, filepath)
        self.emit("buffer_change", {"filepath": filepath})

    def dispatch_buffer_save(self, filepath):
        for cb in list(self._buffer_save_callbacks):
            self._safe_call(cb, filepath)
        self.emit("buffer_save", {"filepath": filepath})
