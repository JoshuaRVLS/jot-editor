"""Plugin lifecycle + dependency/lazy activation runtime for jot_api."""

import inspect
import os
import time
import sys


class PluginRuntime:
    def __init__(
        self,
        safe_call,
        register_event_trigger=None,
        register_command_trigger=None,
        execute_palette_line=None,
        show_message=None,
        host_info=None,
    ):
        self._safe_call = safe_call
        self._register_event_trigger = register_event_trigger
        self._register_command_trigger = register_command_trigger
        self._execute_palette_line = execute_palette_line
        self._show_message = show_message
        self._host_info = host_info
        self._entries = {}
        self._policy_mode = str(os.environ.get("JOT_PLUGIN_POLICY", "off")).strip().lower()
        if self._policy_mode not in {"off", "warn", "strict"}:
            self._policy_mode = "off"
        self._warned_caps = set()
        self._audit = []
        self._audit_limit = 500

    def _entry(self, npath):
        return self._entries.setdefault(
            npath,
            {
                "path": npath,
                "name": os.path.basename(npath),
                "loaded": False,
                "load_count": 0,
                "reload_count": 0,
                "last_error": "",
                "last_load_ms": 0.0,
                "updated_at": 0.0,
                "on_load": None,
                "on_unload": None,
                "on_reload": None,
                "meta_name": "",
                "meta_version": "",
                "meta_author": "",
                "meta_description": "",
                "meta_depends": [],
                "meta_lazy_events": [],
                "meta_lazy_commands": [],
                "meta_eager": False,
                "meta_min_api": "",
                "meta_max_api": "",
                "meta_min_jot": "",
                "meta_max_jot": "",
                "meta_min_python": "",
                "meta_max_python": "",
                "meta_requires_unsafe": False,
                "meta_capabilities": [],
                "setup": None,
                "activated": False,
                "activation_error": "",
                "compat_error": "",
                "lazy_armed": False,
                "command_reentry": False,
            },
        )

    @staticmethod
    def _norm(path):
        if not path:
            return ""
        try:
            return os.path.abspath(path)
        except Exception:
            return str(path)

    @staticmethod
    def _infer_plugin_path(depth=2):
        try:
            frame = inspect.stack()[depth]
            return os.path.abspath(frame.filename)
        except Exception:
            return ""

    @staticmethod
    def _normalize_list(values, lower=False):
        out = []
        if isinstance(values, str):
            values = [values]
        if not isinstance(values, (list, tuple, set)):
            return out
        for item in values:
            text = str(item or "").strip()
            if not text:
                continue
            out.append(text.lower() if lower else text)
        return out

    def _logical_name(self, entry):
        label = (entry.get("meta_name") or "").strip()
        if label:
            return label.lower()

        filename = os.path.basename(entry.get("path") or "")
        if filename in {"plugin.py", "__init__.py"}:
            parent = os.path.basename(os.path.dirname(entry.get("path") or ""))
            return parent.lower()

        stem, _ext = os.path.splitext(filename)
        return stem.lower()

    @staticmethod
    def _normalize_bool(value):
        if isinstance(value, bool):
            return value
        text = str(value or "").strip().lower()
        return text in {"1", "true", "yes", "on"}

    @staticmethod
    def _parse_version(value):
        text = str(value or "").strip()
        if not text:
            return ()
        text = text.lstrip("vV")
        parts = []
        token = ""
        for ch in text:
            if ch.isdigit():
                token += ch
                continue
            if token:
                parts.append(int(token))
                token = ""
            if ch in {".", "-", "_"}:
                continue
            break
        if token:
            parts.append(int(token))
        return tuple(parts)

    @classmethod
    def _version_cmp(cls, left, right):
        l = cls._parse_version(left)
        r = cls._parse_version(right)
        if not l and not r:
            return 0
        if not l:
            return -1
        if not r:
            return 1
        width = max(len(l), len(r))
        l = l + (0,) * (width - len(l))
        r = r + (0,) * (width - len(r))
        if l < r:
            return -1
        if l > r:
            return 1
        return 0

    @classmethod
    def _within_range(cls, current, min_v="", max_v=""):
        if min_v and cls._version_cmp(current, min_v) < 0:
            return False
        if max_v and cls._version_cmp(current, max_v) > 0:
            return False
        return True

    def _get_host_info(self):
        if callable(self._host_info):
            try:
                info = self._host_info() or {}
                if isinstance(info, dict):
                    return info
            except Exception:
                pass
        return {
            "api_version": "1.0.0",
            "jot_version": "0.1.0",
            "python_version": f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}",
        }

    def _compatibility_error(self, entry):
        info = self._get_host_info()
        api_v = info.get("api_version", "")
        jot_v = info.get("jot_version", "")
        py_v = info.get("python_version", "")

        if not self._within_range(api_v, entry.get("meta_min_api"), entry.get("meta_max_api")):
            return f"incompatible api version {api_v}"
        if not self._within_range(jot_v, entry.get("meta_min_jot"), entry.get("meta_max_jot")):
            return f"incompatible jot version {jot_v}"
        if not self._within_range(py_v, entry.get("meta_min_python"), entry.get("meta_max_python")):
            return f"incompatible python version {py_v}"

        if entry.get("meta_requires_unsafe"):
            allowed = self._normalize_bool(os.environ.get("JOT_ALLOW_UNSAFE_PLUGINS", "0"))
            if not allowed:
                return "requires unsafe plugins (set JOT_ALLOW_UNSAFE_PLUGINS=1)"
        return ""

    def _dependency_index(self):
        idx = {}
        for entry in self._entries.values():
            idx[self._logical_name(entry)] = entry
            idx[(entry.get("name") or "").lower()] = entry
            idx[(entry.get("path") or "").lower()] = entry
        return idx

    def _resolve_caller_entry(self):
        try:
            stack = inspect.stack()
        except Exception:
            return None

        candidates = []
        for frame in stack[2:]:
            fname = self._norm(getattr(frame, "filename", ""))
            if not fname:
                continue
            entry = self._entries.get(fname)
            if entry:
                return entry
            for it in self._entries.values():
                plugin_path = self._norm(it.get("path") or "")
                base = os.path.basename(plugin_path)
                if base not in {"plugin.py", "__init__.py"}:
                    continue
                root = self._norm(os.path.dirname(plugin_path))
                if root and fname.startswith(root + os.sep):
                    candidates.append((len(root), it))
        if not candidates:
            return None
        candidates.sort(key=lambda x: x[0], reverse=True)
        return candidates[0][1]

    @staticmethod
    def _entry_label(entry):
        return entry.get("meta_name") or entry.get("name") or "plugin"

    def _record_audit(self, entry, capability, action, detail):
        item = {
            "ts": time.time(),
            "plugin": self._entry_label(entry) if entry else "unknown",
            "path": entry.get("path") if entry else "",
            "capability": str(capability or "").strip().lower(),
            "action": str(action or "").strip().lower(),
            "detail": str(detail or "").strip(),
        }
        self._audit.append(item)
        if len(self._audit) > self._audit_limit:
            self._audit = self._audit[-self._audit_limit :]

    def set_policy_mode(self, mode):
        text = str(mode or "").strip().lower()
        if text not in {"off", "warn", "strict"}:
            return False
        self._policy_mode = text
        self._warned_caps.clear()
        return True

    def policy_mode(self):
        return self._policy_mode

    def check_capability(self, cap):
        cap_key = str(cap or "").strip().lower()
        if not cap_key or self._policy_mode == "off":
            return True, ""

        entry = self._resolve_caller_entry()
        if not entry:
            return True, ""

        caps = set(entry.get("meta_capabilities") or [])
        plugin_label = self._entry_label(entry)
        if cap_key in caps:
            self._record_audit(entry, cap_key, "allow", "declared")
            return True, ""

        if self._policy_mode == "warn":
            warn_key = f"{entry.get('path')}::{cap_key}"
            if warn_key not in self._warned_caps:
                self._warned_caps.add(warn_key)
                self._record_audit(entry, cap_key, "warn", "missing declaration")
                return True, f"{plugin_label} missing capability '{cap_key}' (allowed in warn mode)"
            return True, ""

        if not caps:
            msg = f"{plugin_label} has no declared capabilities (needs '{cap_key}')"
            self._record_audit(entry, cap_key, "deny", "no capabilities declared")
            return False, msg
        msg = f"{plugin_label} missing capability '{cap_key}'"
        self._record_audit(entry, cap_key, "deny", "missing declaration")
        return False, msg

    def _activate_entry(self, entry, reason="startup", stack=None):
        if not entry.get("loaded"):
            return False
        if entry.get("activated"):
            return True

        if stack is None:
            stack = []

        key = self._logical_name(entry)
        if key in stack:
            entry["activation_error"] = "cyclic dependency"
            return False

        compat_error = self._compatibility_error(entry)
        if compat_error:
            entry["compat_error"] = compat_error
            entry["activation_error"] = compat_error
            return False
        entry["compat_error"] = ""

        dep_index = self._dependency_index()
        for dep in entry.get("meta_depends", []):
            dep_key = str(dep or "").strip().lower()
            if not dep_key:
                continue
            dep_entry = dep_index.get(dep_key)
            if not dep_entry:
                entry["activation_error"] = f"missing dependency: {dep}"
                return False
            if not self._activate_entry(dep_entry, reason=f"dep:{dep_key}", stack=stack + [key]):
                entry["activation_error"] = dep_entry.get("activation_error") or f"dependency failed: {dep}"
                return False

        setup = entry.get("setup")
        if callable(setup):
            try:
                setup(reason)
            except TypeError:
                setup()
            except Exception as exc:
                entry["activation_error"] = str(exc)
                return False

        if entry.get("on_load"):
            self._safe_call(entry["on_load"])

        entry["activated"] = True
        entry["activation_error"] = ""
        return True

    def _arm_lazy_entry(self, entry):
        if entry.get("lazy_armed"):
            return

        for event_name in entry.get("meta_lazy_events", []):
            if not callable(self._register_event_trigger):
                continue

            def _event_loader(_payload=None, _entry=entry, _ev=event_name):
                self._activate_entry(_entry, reason=f"event:{_ev}")

            self._register_event_trigger(event_name, _event_loader)

        for command_name in entry.get("meta_lazy_commands", []):
            if not callable(self._register_command_trigger):
                continue

            def _command_loader(arg="", _entry=entry, _cmd=command_name):
                return self._activate_from_command(_entry, _cmd, arg)

            self._register_command_trigger(command_name, _command_loader)

        entry["lazy_armed"] = True

    def _activate_from_command(self, entry, command_name, arg=""):
        ok = self._activate_entry(entry, reason=f"command:{command_name}")
        if not ok:
            if callable(self._show_message):
                msg = entry.get("activation_error") or "plugin activation failed"
                self._show_message(f"Plugin activation failed: {msg}")
            return False

        if not callable(self._execute_palette_line):
            return True
        if entry.get("command_reentry"):
            return False

        line = f"{command_name} {arg}".strip()
        entry["command_reentry"] = True
        try:
            self._execute_palette_line(line)
        finally:
            entry["command_reentry"] = False
        return True

    def clear(self):
        self._entries.clear()

    def on_loaded(self, path, ok=True, error="", load_ms=0.0):
        npath = self._norm(path)
        if not npath:
            return

        entry = self._entry(npath)
        entry["loaded"] = bool(ok)
        entry["last_error"] = "" if ok else (error or "plugin load failed")
        entry["last_load_ms"] = float(load_ms or 0.0)
        entry["updated_at"] = time.time()
        if ok:
            entry["load_count"] += 1

    def register_lifecycle(self, plugin_path=None, on_load=None, on_unload=None, on_reload=None):
        npath = self._norm(plugin_path) if plugin_path else self._infer_plugin_path(3)
        if not npath:
            return

        entry = self._entry(npath)
        if callable(on_load):
            entry["on_load"] = on_load
        if callable(on_unload):
            entry["on_unload"] = on_unload
        if callable(on_reload):
            entry["on_reload"] = on_reload

    def register_metadata(
        self,
        plugin_path=None,
        name="",
        version="",
        author="",
        description="",
        depends=None,
        min_api="",
        max_api="",
        min_jot="",
        max_jot="",
        min_python="",
        max_python="",
        requires_unsafe=False,
        capabilities=None,
        setup=None,
        lazy_events=None,
        lazy_commands=None,
        eager=False,
    ):
        npath = self._norm(plugin_path) if plugin_path else self._infer_plugin_path(3)
        if not npath:
            return

        entry = self._entry(npath)
        entry["meta_name"] = str(name or "").strip()
        entry["meta_version"] = str(version or "").strip()
        entry["meta_author"] = str(author or "").strip()
        entry["meta_description"] = str(description or "").strip()
        entry["meta_depends"] = self._normalize_list(depends, lower=True)
        entry["meta_min_api"] = str(min_api or "").strip()
        entry["meta_max_api"] = str(max_api or "").strip()
        entry["meta_min_jot"] = str(min_jot or "").strip()
        entry["meta_max_jot"] = str(max_jot or "").strip()
        entry["meta_min_python"] = str(min_python or "").strip()
        entry["meta_max_python"] = str(max_python or "").strip()
        entry["meta_requires_unsafe"] = self._normalize_bool(requires_unsafe)
        entry["meta_capabilities"] = self._normalize_list(capabilities, lower=True)
        entry["meta_lazy_events"] = self._normalize_list(lazy_events, lower=True)
        entry["meta_lazy_commands"] = self._normalize_list(lazy_commands, lower=True)
        entry["meta_eager"] = bool(eager)
        if callable(setup):
            entry["setup"] = setup

    def before_reload(self):
        for entry in self._entries.values():
            if entry.get("on_unload"):
                self._safe_call(entry["on_unload"])
            if entry.get("on_reload"):
                self._safe_call(entry["on_reload"])
            entry["reload_count"] += 1
            entry["activated"] = False
            entry["activation_error"] = ""
            entry["compat_error"] = ""
            entry["lazy_armed"] = False
            entry["command_reentry"] = False

    def finalize_activation(self):
        activated = 0
        lazy = 0
        failed = 0
        entries = sorted(self._entries.values(), key=lambda e: (e.get("meta_name") or e.get("name") or "").lower())
        for entry in entries:
            if not entry.get("loaded"):
                failed += 1
                continue

            has_lazy = bool(entry.get("meta_lazy_events") or entry.get("meta_lazy_commands"))
            if has_lazy and not entry.get("meta_eager"):
                self._arm_lazy_entry(entry)
                lazy += 1
                continue

            if self._activate_entry(entry, reason="startup"):
                activated += 1
            else:
                failed += 1
                entry["last_error"] = entry.get("activation_error") or entry.get("last_error") or "activation failed"

        return {"activated": activated, "lazy": lazy, "failed": failed}

    def health(self):
        items = []
        for entry in self._entries.values():
            items.append(
                {
                    "path": entry["path"],
                    "name": entry["name"],
                    "loaded": entry["loaded"],
                    "activated": entry["activated"],
                    "lazy_armed": entry["lazy_armed"],
                    "load_count": entry["load_count"],
                    "reload_count": entry["reload_count"],
                    "last_error": entry["last_error"],
                    "last_load_ms": entry["last_load_ms"],
                    "activation_error": entry["activation_error"],
                    "compat_error": entry.get("compat_error", ""),
                    "meta_name": entry.get("meta_name", ""),
                    "meta_version": entry.get("meta_version", ""),
                    "meta_author": entry.get("meta_author", ""),
                    "meta_description": entry.get("meta_description", ""),
                    "meta_depends": list(entry.get("meta_depends", [])),
                    "meta_min_api": entry.get("meta_min_api", ""),
                    "meta_max_api": entry.get("meta_max_api", ""),
                    "meta_min_jot": entry.get("meta_min_jot", ""),
                    "meta_max_jot": entry.get("meta_max_jot", ""),
                    "meta_min_python": entry.get("meta_min_python", ""),
                    "meta_max_python": entry.get("meta_max_python", ""),
                    "meta_requires_unsafe": bool(entry.get("meta_requires_unsafe")),
                    "meta_capabilities": list(entry.get("meta_capabilities", [])),
                    "meta_lazy_events": list(entry.get("meta_lazy_events", [])),
                    "meta_lazy_commands": list(entry.get("meta_lazy_commands", [])),
                }
            )
        items.sort(key=lambda x: (x["meta_name"] or x["name"]).lower())
        return items

    def health_summary(self):
        items = self.health()
        if not items:
            return "Plugins: none"

        loaded = sum(1 for it in items if it["loaded"])
        active = sum(1 for it in items if it["activated"])
        lazy = sum(1 for it in items if it["lazy_armed"] and not it["activated"])
        failed = len(items) - loaded + sum(
            1 for it in items if it["loaded"] and it.get("activation_error")
        )

        top = []
        for it in items[:6]:
            if it["activated"]:
                status = "ON"
            elif it["lazy_armed"]:
                status = "LAZY"
            elif it["loaded"]:
                status = "LOAD"
            else:
                status = "ERR"

            ms = f"{it['last_load_ms']:.1f}ms"
            label = it.get("meta_name") or it["name"]
            if it.get("meta_version"):
                label = f"{label}@{it['meta_version']}"
            part = f"{label}[{status},{ms}]"
            err = it.get("compat_error") or it.get("activation_error") or it.get("last_error") or ""
            if err and status == "ERR":
                part += ":" + err.splitlines()[0][:48]
            top.append(part)

        return (
            f"Plugins: {loaded} loaded, {active} active, {lazy} lazy, {failed} failed | "
            + " | ".join(top)
        )

    def info(self, query=""):
        q = str(query or "").strip().lower()
        items = self.health()
        if not q:
            return items
        filtered = []
        for item in items:
            name = (item.get("meta_name") or item.get("name") or "").lower()
            path = (item.get("path") or "").lower()
            if q in name or q in path:
                filtered.append(item)
        return filtered

    def policy_summary(self):
        caps_declared = 0
        for it in self._entries.values():
            if it.get("meta_capabilities"):
                caps_declared += 1
        return (
            f"PluginPolicy: mode={self._policy_mode}, declared={caps_declared}/{len(self._entries)}, "
            f"audit={len(self._audit)}"
        )

    def audit(self, limit=20, query=""):
        try:
            lim = int(limit)
        except Exception:
            lim = 20
        if lim < 1:
            lim = 1
        if lim > 200:
            lim = 200

        q = str(query or "").strip().lower()
        items = list(reversed(self._audit))
        if q:
            filtered = []
            for item in items:
                blob = " ".join(
                    [
                        item.get("plugin", ""),
                        item.get("path", ""),
                        item.get("capability", ""),
                        item.get("action", ""),
                        item.get("detail", ""),
                    ]
                ).lower()
                if q in blob:
                    filtered.append(item)
            items = filtered
        return items[:lim]

    def clear_audit(self):
        self._audit.clear()
