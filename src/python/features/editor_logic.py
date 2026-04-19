"""Editor feature logic migrated from C++ to Python.

This module is intentionally pure and stateless so C++ can call into it
for hot-path decisions while keeping the C++ host thin.
"""


def should_auto_close(ch):
    return ch in ("(", "{", "[", '"', "'")


def get_closing_bracket(ch):
    mapping = {
        "(": ")",
        "{": "}",
        "[": "]",
        '"': '"',
        "'": "'",
    }
    return mapping.get(ch, "")


def is_closing_bracket(ch):
    return ch in (")", "}", "]", '"', "'")


def should_skip_closing(ch, line, pos):
    if pos < 0 or pos >= len(line):
        return False
    return line[pos] == ch


def _trim_left(s):
    return s.lstrip(" \t")


def _trim_right_ws(s):
    return s.rstrip(" \t")


def _starts_with_keyword(line, keyword):
    if not line.startswith(keyword):
        return False
    if len(line) == len(keyword):
        return True
    next_ch = line[len(keyword)]
    return not (next_ch.isalnum() or next_ch == "_")


def should_auto_indent(line):
    trimmed = _trim_left(line)
    if not trimmed:
        return False

    trimmed = _trim_right_ws(trimmed)
    if not trimmed:
        return False

    comment_pos = trimmed.find("//")
    if comment_pos != -1:
        trimmed = _trim_right_ws(trimmed[:comment_pos])
    if not trimmed:
        return False

    if trimmed[-1] in ("{", "[", "(", ":"):
        return True

    keywords = (
        "if",
        "for",
        "while",
        "else",
        "def",
        "class",
        "switch",
        "case",
        "default",
        "try",
        "catch",
        "do",
        "finally",
    )
    for kw in keywords:
        if _starts_with_keyword(trimmed, kw):
            return True
    return False


def should_dedent(line):
    trimmed = _trim_left(line)
    if not trimmed:
        return False

    if trimmed[0] in ("}", "]", ")"):
        return True

    for kw in ("else", "catch", "finally", "case", "default"):
        if _starts_with_keyword(trimmed, kw):
            return True
    return False


def find_matching_bracket(lines, line, col, open_char, close_char):
    if line < 0 or line >= len(lines):
        return -1
    row = lines[line]
    if col < 0 or col >= len(row):
        return -1

    ch = row[col]
    if ch != open_char and ch != close_char:
        return -1

    direction = 1 if ch == open_char else -1
    depth = 1
    cur_line = line
    cur_col = col

    while 0 <= cur_line < len(lines):
        cur_row = lines[cur_line]
        while 0 <= cur_col < len(cur_row):
            c = cur_row[cur_col]
            if c == open_char:
                depth += direction
            if c == close_char:
                depth -= direction
            if depth == 0:
                return cur_line * 10000 + cur_col
            cur_col += direction

        if direction > 0:
            cur_line += 1
            cur_col = 0
        else:
            cur_line -= 1
            if cur_line >= 0:
                cur_col = len(lines[cur_line]) - 1
    return -1

