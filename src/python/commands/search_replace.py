"""Search and replace commands."""

import os
import re
import shlex


def register_search_replace_commands(api):
    register_command = api["register_command"]
    register_palette_command = api["register_palette_command"]
    show_message = api["show_message"]
    get_buffer_content = api.get("get_buffer_content")
    set_buffer_content = api.get("set_buffer_content")
    get_current_file = api.get("get_current_file")

    def _parse_replace_args(arg):
        text = (arg or "").strip()
        if not text:
            return (
                None,
                None,
                None,
                "usage: sreplace /find/repl/flags or sreplace \"find\" \"repl\" [flags]",
            )

        if len(text) >= 3 and not text[0].isalnum() and not text[0].isspace():
            delim = text[0]
            idx = 1
            parts = []
            current = []
            escaped = False
            while idx < len(text):
                ch = text[idx]
                idx += 1
                if escaped:
                    current.append(ch)
                    escaped = False
                    continue
                if ch == "\\":
                    escaped = True
                    continue
                if ch == delim:
                    parts.append("".join(current))
                    current = []
                    if len(parts) == 2:
                        break
                    continue
                current.append(ch)

            if len(parts) < 2:
                return None, None, None, "invalid replace expression"
            flags = text[idx:].strip() if idx <= len(text) else ""
            return parts[0], parts[1], flags, ""

        try:
            parts = shlex.split(text)
        except ValueError as exc:
            return None, None, None, f"invalid args: {exc}"
        if len(parts) < 2:
            return None, None, None, "usage: sreplace \"find\" \"repl\" [flags]"
        query = parts[0]
        repl = parts[1]
        flags = parts[2] if len(parts) > 2 else ""
        return query, repl, flags, ""

    def _compile_search_pattern(query, flags):
        if query is None:
            return None, "missing query"
        flagset = set((flags or "").lower())
        use_regex = "r" in flagset
        whole_word = "w" in flagset
        case_insensitive = "i" in flagset

        pattern_text = query if use_regex else re.escape(query)
        if whole_word:
            pattern_text = r"\b(?:" + pattern_text + r")\b"

        re_flags = re.MULTILINE
        if case_insensitive:
            re_flags |= re.IGNORECASE

        try:
            pattern = re.compile(pattern_text, re_flags)
        except re.error as exc:
            return None, f"regex error: {exc}"
        return pattern, ""

    def _line_number_from_offset(content, offset):
        return content.count("\n", 0, max(0, offset)) + 1

    def _cmd_sfind(arg=""):
        if not callable(get_buffer_content):
            show_message("sfind unavailable")
            return True

        text = (arg or "").strip()
        if not text:
            show_message(
                "usage: sfind \"query\" [flags], flags: i=ignorecase r=regex w=word"
            )
            return True
        try:
            parts = shlex.split(text)
        except ValueError as exc:
            show_message(f"sfind args error: {exc}")
            return True
        query = parts[0]
        flags = parts[1] if len(parts) > 1 else ""
        pattern, err = _compile_search_pattern(query, flags)
        if not pattern:
            show_message(f"sfind failed: {err}")
            return True

        content = get_buffer_content() or ""
        matches = list(pattern.finditer(content))
        file_name = ""
        if callable(get_current_file):
            file_name = os.path.basename(get_current_file() or "")
        where = file_name or "buffer"
        if not matches:
            show_message(f"sfind: no matches in {where}")
            return True

        lines = []
        for m in matches[:6]:
            lines.append(str(_line_number_from_offset(content, m.start())))
        show_message(
            f"sfind: {len(matches)} match(es) in {where} at line(s): "
            + ",".join(lines)
        )
        return True

    def _cmd_sreplace(arg="", force_all=False):
        if not callable(get_buffer_content) or not callable(set_buffer_content):
            show_message("sreplace unavailable")
            return True

        query, repl, flags, err = _parse_replace_args(arg)
        if err:
            show_message(f"sreplace: {err}")
            return True
        pattern, err = _compile_search_pattern(query, flags)
        if not pattern:
            show_message(f"sreplace failed: {err}")
            return True

        content = get_buffer_content() or ""
        flagset = set((flags or "").lower())
        replace_all = force_all or ("g" in flagset)
        count_limit = 0 if replace_all else 1
        use_regex = "r" in flagset

        if use_regex:
            new_content, changed = pattern.subn(repl, content, count=count_limit)
        else:
            # Keep replacement literal for non-regex mode.
            new_content, changed = pattern.subn(
                lambda _m: repl, content, count=count_limit
            )

        if changed <= 0:
            show_message("sreplace: no matches")
            return True

        set_buffer_content(new_content)
        mode = "all" if replace_all else "single"
        show_message(f"sreplace: replaced {changed} occurrence(s) [{mode}]")
        return True

    def _cmd_sreplace_one(arg=""):
        return _cmd_sreplace(arg, force_all=False)

    def _cmd_sreplace_all(arg=""):
        return _cmd_sreplace(arg, force_all=True)

    register_command("SFind", _cmd_sfind)
    register_command("SReplace", _cmd_sreplace_one)
    register_command("SReplaceAll", _cmd_sreplace_all)

    register_palette_command("sfind", _cmd_sfind)
    register_palette_command("sreplace", _cmd_sreplace_one)
    register_palette_command("sreplaceall", _cmd_sreplace_all)
