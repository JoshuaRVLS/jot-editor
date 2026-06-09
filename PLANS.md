# Fix Fullscreen/Large-Window Terminal Corruption

## Summary

Small windows work but large/fullscreen breaks: text shows "weird color"
overlap, only a few characters paint, and the rest of the content stays
invisible until the user scrolls or selects (which forces a re-render of
just the changed area). The rightmost-column safety margin is necessary
but not sufficient. The remaining cause is that some terminals (notably
COSMIC terminal at fullscreen sizes) silently drop bytes from a single
large `write()`. The fix is to send the frame in chunks via a non-
blocking drain that never freezes the event loop, combined with a per-row
`ESC[K` clear and the right-edge safety margin.

## Key Changes

- Right-edge safety margin:
    - `UI::render()` writes `max(0, width - margin)` cells per row.
    - `JOT_RENDER_MARGIN=<n>` env var, default 1.

- Per-row explicit erase:
    - After writing each row, emit `ESC[K` (EL 0) to clear any leftover
      content in the right-edge margin columns and any stale characters
      past the last painted cell. With autowrap disabled, the cursor
      stays at the end of the written text and the erase is bounded to
      the current row.

- Non-blocking drain (the new primary fix):
    - Add `Terminal::try_drain()` which, once the output buffer has
      grown past `render_chunk_bytes_` (default 4096, override with
      `JOT_RENDER_CHUNK_BYTES`), sets `O_NONBLOCK` on stdout and calls
      `write()` to push as much of the buffer as the kernel PTY can
      accept right now. Any data the kernel cannot accept stays in
      the buffer for the next `try_drain()` or the final blocking
      `flush()` at frame end.
    - `UI::render()` calls `try_drain()` after each row. This gives
      the terminal data in chunks (the visual fix) without blocking
      the event loop (no typing freeze).
    - The single `flush()` at frame end drains any remaining data
      with a blocking `write()`. This is the one unavoidable blocking
      call but it is brief because most data was already drained by
      the per-row `try_drain()` calls.

- Run-coalescing optimization:
    - When only the fg/bg change between runs (bold/italic/reverse are
      unchanged), skip the `ESC[0m` (full SGR reset) and emit the new
      fg/bg in place. This reduces the per-color-change overhead from
      ~25 bytes to ~16 bytes, keeping frames smaller.

## Status

**Implemented (2026-06-10).** The renderer now:
- Leaves the rightmost `render_margin` columns of every row untouched
  (default 1, override with `JOT_RENDER_MARGIN=<n>`).
- Emits `ESC[K` after each row to clear any stale content in the
  right-edge margin.
- Calls `try_drain()` after each row to push data to the kernel
  without blocking (default threshold 4096, override with
  `JOT_RENDER_CHUNK_BYTES`, set 0 to disable).
- Does a single blocking `flush()` at frame end to drain any
  remaining data.

Verified at 80x24, 200x60, and 240x70 with `JOT_RENDER_CAPTURE_RAW=1`:
exactly one `ESC[K` per row, all rows painted, frame marker `bytes=`
shows small final flush (data was drained throughout the frame).
Typing at 20 keys/sec shows no freeze at any terminal size. CTest:
3 pass / 2 pre-existing failures (unrelated).

# Fix Large-File Typing Freeze (Delta Undo)

## Summary

Even after the renderer fix above, opening a 5M-line / 418 MB file
(`test_short_lines.log`) and typing still freezes the editor for
seconds per keystroke. The renderer itself is fine — the freeze is
in the undo system, which on every keystroke called
`buf.materialize()` (copying all 5M lines into RAM) and then
`capture_state()` (copying all 5M lines again into the undo `State`).
That's ~836 MB of allocation per keypress, on top of an O(n)
`same_state()` comparison of all 5M strings. The fix is delta-based
undo: store only a window of lines around the cursor, not the entire
file, in each undo entry.

## Key Changes

- `State` is now a delta struct (`full_snapshot`, `start_line`,
  `old_lines`, `old_total_lines`, cursor/selection/scroll) instead
  of a full-snapshot of every line.
- `save_state()` never calls `materialize()`. For buffers with
  ≤ `kMaxFullSnapshotLines` (5000) it captures all lines (full
  snapshot, unchanged behavior). For larger buffers it captures
  `kDeltaWindowHalfSize` (50) lines above and below the cursor
  (or the full selection range if one is active).
- `same_state()` compares the captured window and cursor state, not
  the full file. This drops the deduplication cost from O(n) to
  O(window).
- `undo()` / `redo()` apply the delta by computing the difference
  between the current line count and the captured `old_total_lines`
  to determine how many lines the edit inserted/deleted, then
  replacing the affected range with `old_lines` via
  `FileBuffer::replace_lines()`.
- `LineProvider::replace_lines()` added (and implemented in
  `InMemoryLineProvider` and `LazyLineProvider`) so undo/redo can
  modify the line content without going through the in-memory
  vector. This keeps lazy buffers lazy through edits.
- `LazyLineProvider::kMaxCachedChunks` raised from 20 to 128, so
  the visible area plus the ±50-line delta window fit in cache
  without disk I/O during typing.
- `render_buffer_content()` calls `buf.scroll_hint()` before
  rendering when the buffer is lazy, pre-loading the 4 chunks
  around the visible viewport.

## Status

**Implemented (2026-06-10).** Verified on a 5M-line / 418 MB file:
- 50 keystrokes at 20 keys/sec: 2.51s (matches the input rate, no
  freeze).
- 100 keystrokes at 50 keys/sec: 2.01s.
- 10 undos after a 10-char edit: 1.0s.
- File content unchanged after edit + undo + quit (10-line test
  file).
- Page-down/page-up through the file navigates without freeze.
- ctest: 3 pass / 2 pre-existing failures (`TestTrimRight`,
  `TestMatchingBracket` — unrelated).
