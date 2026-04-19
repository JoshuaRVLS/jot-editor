"""Plugin administration commands."""

from pathlib import Path


def register_plugin_admin_commands(api):
    register_command = api["register_command"]
    show_message = api["show_message"]
    reload_plugins = api["reload_plugins"]
    list_plugins = api["list_plugins"]
    plugin_health_summary = api.get("plugin_health_summary")
    plugin_info = api.get("plugin_info")
    plugin_policy = api.get("plugin_policy")
    plugin_audit = api.get("plugin_audit")
    plugin_audit_clear = api.get("plugin_audit_clear")

    def _plugin_reload_command(_arg=""):
        count = reload_plugins()
        show_message(
            f"Python runtime reloaded (user plugins: {count}). Built-in Python features are always available."
        )

    def _plugin_list_command(_arg=""):
        plugins = list_plugins()
        if not plugins:
            show_message("No user plugins loaded")
            return
        show_message("Plugins: " + ", ".join(Path(p).name for p in plugins[:6]))

    def _plugin_health_command(_arg=""):
        if callable(plugin_health_summary):
            show_message(plugin_health_summary())
        else:
            show_message("Plugin health unavailable")

    def _plugin_info_command(arg=""):
        if not callable(plugin_info):
            show_message("Plugin info unavailable")
            return
        items = plugin_info((arg or "").strip())
        if not items:
            show_message("No plugin matches")
            return
        item = items[0]
        label = item.get("meta_name") or item.get("name") or "plugin"
        version = item.get("meta_version") or "dev"
        if item.get("activated"):
            status = "active"
        elif item.get("lazy_armed"):
            status = "lazy"
        else:
            status = "loaded" if item.get("loaded") else "failed"
        ms = float(item.get("last_load_ms") or 0.0)
        msg = f"{label}@{version} [{status}] {ms:.1f}ms"
        if item.get("meta_author"):
            msg += f" by {item['meta_author']}"
        if item.get("meta_depends"):
            msg += " deps: " + ",".join(item["meta_depends"][:4])
        if item.get("meta_capabilities"):
            msg += " caps: " + ",".join(item["meta_capabilities"][:4])
        compat = []
        if item.get("meta_min_api") or item.get("meta_max_api"):
            compat.append(
                f"api[{item.get('meta_min_api') or '-'}..{item.get('meta_max_api') or '-'}]"
            )
        if item.get("meta_min_jot") or item.get("meta_max_jot"):
            compat.append(
                f"jot[{item.get('meta_min_jot') or '-'}..{item.get('meta_max_jot') or '-'}]"
            )
        if item.get("meta_min_python") or item.get("meta_max_python"):
            compat.append(
                f"py[{item.get('meta_min_python') or '-'}..{item.get('meta_max_python') or '-'}]"
            )
        if item.get("meta_requires_unsafe"):
            compat.append("unsafe")
        if compat:
            msg += " compat: " + ",".join(compat[:3])
        if item.get("compat_error"):
            msg += " compat_err: " + item["compat_error"].splitlines()[0][:64]
        if item.get("last_error"):
            msg += " err: " + item["last_error"].splitlines()[0][:64]
        show_message(msg)

    def _plugin_policy_command(arg=""):
        if not callable(plugin_policy):
            show_message("Plugin policy unavailable")
            return
        mode = (arg or "").strip()
        result = plugin_policy(mode if mode else None)
        show_message(result)

    def _plugin_audit_command(arg=""):
        if not callable(plugin_audit):
            show_message("Plugin audit unavailable")
            return
        raw = (arg or "").strip()
        limit = 12
        query = ""
        if raw:
            parts = raw.split(None, 1)
            try:
                limit = int(parts[0])
                if len(parts) > 1:
                    query = parts[1].strip()
            except ValueError:
                query = raw
        items = plugin_audit(limit=limit, query=query)
        if not items:
            show_message("PluginAudit: empty")
            return
        top = []
        for item in items[:4]:
            plugin = item.get("plugin", "plugin")
            action = item.get("action", "event")
            cap = item.get("capability", "?")
            detail = item.get("detail", "")
            part = f"{plugin}:{action}:{cap}"
            if detail:
                part += f"({detail[:24]})"
            top.append(part)
        show_message("PluginAudit: " + " | ".join(top))

    def _plugin_audit_clear_command(_arg=""):
        if not callable(plugin_audit_clear):
            show_message("Plugin audit unavailable")
            return
        plugin_audit_clear()
        show_message("PluginAudit: cleared")

    register_command("PlugReload", _plugin_reload_command)
    register_command("PlugList", _plugin_list_command)
    register_command("PlugHealth", _plugin_health_command)
    register_command("PlugInfo", _plugin_info_command)
    register_command("PlugPolicy", _plugin_policy_command)
    register_command("PlugAudit", _plugin_audit_command)
    register_command("PlugAuditClear", _plugin_audit_clear_command)
