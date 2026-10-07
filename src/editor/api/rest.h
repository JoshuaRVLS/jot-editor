// ---------------------------------------------------------------------------
// The HTTP client (.http / .rest request files)
// ---------------------------------------------------------------------------
//
// `:rest` runs the request at the cursor out of an IntelliJ-style request file
// (rest.nvim's domain), `:rest <name>` a named one, `:rest last` sends the
// previous request again. The curl runs on the worker queue so a slow endpoint
// never stalls a keystroke; what comes back lands in a `[Response]` scratch
// tab that later runs re-use (see jot/app/rest_client.cpp, and
// features/http_file.h for the file format and the pure half).
//
// A fragment of the Editor class body, included by src/editor/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
private:
// Requests are quick to time out by REST-client standards: this is an
// editor, and a hung endpoint must not hold the "one at a time" slot open.
static constexpr int kRestTimeoutSeconds = 30;

void rest_run(const std::string &arg);
// Everything a run does before the network: parse the buffer, pick the
// request (`name` empty = the one at the cursor), read an external body file
// and resolve the variables. False reports why on the message line -- an
// unresolved `{{var}}` refuses the run rather than sending it wrong.
bool rest_prepare(const std::string &name, HttpFile::Resolved &out);
void rest_send(HttpFile::Resolved resolved);
void rest_show_response(const HttpFile::Resolved &resolved, const std::string &output);
// The response tab for this run's payload type, created on first use and
// re-used (content swapped) from then on.
int rest_response_buffer(const std::string &extension);
