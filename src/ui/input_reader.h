#ifndef UI_INPUT_READER_H
#define UI_INPUT_READER_H

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>

// The one reader of the input fd. A sequence whose head a reader consumes must
// come back for its tail; abandon() finishes the tail if it has arrived and
// claims it if it has not, every read settles the claim first, and a byte that
// was read but not claimed is handed back rather than lost. That is what stops
// the tail of a reply ("191R", ";12;3M") from being read back as typing.
//
// Kept free of Terminal so it is unit testable against a pipe.

namespace jot_ui
{

// The grammar of a tail still owed, so the bytes skipped are provably the
// sequence the head belonged to and never a keystroke that raced it.
//
// The last three are the shapes only a terminal sends. They keep their claims
// far longer than the others (see kMachineTailMs) because a terminal's writes
// are chopped by a full pty while the app is drawing, and the app can spend
// seconds on a frame between a report's head and its tail. Each keeps its own
// narrow grammar so that holding the claim longer cannot swallow typing.
enum class OwedTail
{
  CsiBody,     // parameter and intermediate bytes, then any final byte
  FinalByte,   // a single final byte: the last byte of an SS3 sequence
  MouseReport, // digits, `;` and `?`, then `M` or `m`: an SGR mouse report
  KeyReport,   // digits, `;` and `:`, then `u`: a kitty keyboard report
  Reply,       // a terminal's own answer, head unread: a mouse report, a key
               // report, a cursor report (`R`), a device-attributes answer
               // (`c`), a focus event (`I`/`O`), or a paste marker (`~`)
};

class InputReader
{
public:
  // How long abandon() waits for a tail that may already be queued, and how
  // long the claim it leaves behind stays open when it is not.
  static constexpr int kTailPerByteMs = 20;
  static constexpr int kTailBudgetMs = 50;
  static constexpr int kClaimMs = 1000;

  // How long a claim on what a terminal sends waits for its tail. A terminal's
  // report is machine traffic, and its tail can be a whole frame late: the
  // terminal writes one in a single call, the pty takes it in pieces when the
  // app is not draining it, and the app can then spend seconds drawing before
  // it reads again. The one-second window the other tails get expired inside
  // that gap, and the tail was typed into the buffer instead ("35;30;19M" and
  // "105;5u", the run of either a pointer storm or a keypress leaves).
  static constexpr int kMachineTailMs = 10000;

  // `claim_ms` is how long a claim outlives the drain and `machine_claim_ms`
  // is the same for the shapes only a terminal sends; the defaults are the
  // editor's, and a test passes short ones instead of waiting them out.
  explicit InputReader(int fd, int claim_ms = kClaimMs,
                       int machine_claim_ms = kMachineTailMs);

  // The next byte: one handed back, else one off the fd. A negative timeout
  // does not wait, and a handed-back byte never waits.
  bool read(char &out, int timeout_ms);

  // Gives bytes back: the next read takes them, in this order, ahead of the fd.
  // This is what lets a reader that has to pull bytes it cannot yet judge put
  // them back instead of dropping them.
  void unread(char c);
  void unread(const char *bytes, std::size_t len);

  // Whether bytes are waiting to be handed back.
  bool pending() const
  {
    return !pending_.empty();
  }

  // Eats the rest of a sequence whose head was consumed and dropped.
  void abandon(OwedTail owed, int per_byte_ms = kTailPerByteMs,
               int budget_ms = kTailBudgetMs);

  // The same, for a tail with a fixed shape rather than a grammar, such as the
  // `~` of a bracketed paste's terminator after its body stopped.
  void claim_remaining(const char *bytes, std::size_t consumed, std::size_t len);

  // Finishes a claimed tail with what has arrived: eats the bytes that belong
  // to it, hands back the first one that does not, and drops the claim at its
  // deadline. Called before every read.
  void settle();

  // Reads a bracketed paste body up to, and including, its terminator `ESC [
  // 201 ~`. A body that stops before its terminator keeps what it did send and
  // has that terminator's remaining bytes claimed; false means no body at all.
  bool read_paste(std::string &out);

  bool claiming() const
  {
    return claim_ != Claim::None;
  }

private:
  enum class Claim
  {
    None,
    Grammar,
    Fixed,
  };

  bool accepts(char c, bool &is_final);
  void open_claim();

  // More than any caller hands back at once: the size probe returns at most
  // its own 32-byte buffer, and a drain or a claim returns one byte.
  static constexpr std::size_t kPendingMax = 256;

  int fd_;
  int claim_ms_;
  int machine_claim_ms_;
  std::deque<char> pending_;
  Claim claim_ = Claim::None;
  OwedTail owed_ = OwedTail::CsiBody;
  char fixed_[8] = {};
  std::size_t fixed_len_ = 0;
  std::size_t fixed_at_ = 0;
  std::chrono::steady_clock::time_point deadline_{};
};

} // namespace jot_ui

#endif
