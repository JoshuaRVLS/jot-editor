#ifndef UI_CURSOR_BLINK_H
#define UI_CURSOR_BLINK_H

// The caret's blink clock. One clock for both frontends, so the terminal and the
// GUI blink in the same phase at the configured rate (cursor_blink_ms) instead of
// each doing its own thing -- the terminal used to hand the phase to the emulator
// via a blinking DECSCUSR shape (ignoring the config entirely), and the GUI never
// blinked at all.
//
// Kept free of SDL/Terminal so it is unit testable on its own.

namespace jot_ui
{
  // True while the caret should be shown. The phase is measured from
  // `anchor_ms`, which the editor re-anchors whenever the caret moves or the user
  // types, so a caret that just landed is always visible. `period_ms` is one half
  // of the cycle (visible for the first half, hidden for the second); <= 0 means
  // "never blink". `hold_visible` forces the caret solid -- the configured steady
  // cursor styles, and the brief hold after input.
  bool blink_phase_visible(long long now_ms,
                           long long anchor_ms,
                           int period_ms,
                           bool hold_visible);
} // namespace jot_ui

#endif // UI_CURSOR_BLINK_H
