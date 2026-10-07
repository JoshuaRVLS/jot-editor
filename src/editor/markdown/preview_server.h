// Markdown preview server: a tiny HTTP/1.1 server that serves the rendered
// markdown page to a browser, streams live updates over Server-Sent Events and
// receives scroll positions back from the page (browser -> editor sync). It runs
// on the editor's own libuv loop, so it adds no threads and every callback (and
// Lua) stays on the main thread.
//
// Routes: `GET /` the page shell, `GET /events` the event stream, `POST /sync`
// and `GET /sync?line=N` the browser's top visible line, `GET /image?path=..` a
// local image, `GET /favicon.ico` a 204. With a file root set the whole tree is
// served by path instead.
#ifndef JOT_MARKDOWN_PREVIEW_SERVER_H
#define JOT_MARKDOWN_PREVIEW_SERVER_H

#include <memory>
#include <string>

class EventLoop;
struct PreviewServerImpl;

class PreviewServer
{
public:
  explicit PreviewServer(EventLoop *loop);
  ~PreviewServer();

  PreviewServer(const PreviewServer &) = delete;
  PreviewServer &operator=(const PreviewServer &) = delete;

  // Binds and listens. `port` 0 asks the OS for a free port; the chosen port is
  // returned through `out_port`. Returns false and fills `error` on failure.
  bool start(const std::string &host, int port, int *out_port, std::string *error);
  void stop();

  bool running() const;
  int port() const;
  int client_count() const;

  // The page served at "/". Stored until replaced.
  void set_page(std::string html);
  const std::string &page() const;

  // The rendered document body. This is what the EventSource pushes as the
  // `content` event, so the page can swap its inner HTML without a reload.
  void set_content(std::string body);
  const std::string &content() const;

  // Serving a whole directory instead of one stored page (what an HTML preview
  // needs): the page's relative references have to resolve against the root the
  // file lives in, so `GET /<path>` serves a file under it (a climb out is
  // refused) and an HTML response gets the live-reload client appended.
  void set_file_root(std::string dir);
  const std::string &file_root() const;

  // The document served in place of the file on disk, for the one path being
  // edited: the buffer's text, unsaved changes and all. Every other path under
  // the root still comes from the file system, which is what lets the preview
  // show an unsaved edit without the page's stylesheet disappearing.
  void set_document(std::string relative_path, std::string text);
  void clear_document();
  const std::string &document_path() const;

  // Broadcasts a Server-Sent Event to every connected page.
  void notify(const std::string &event, const std::string &data);

  // Editor -> preview scroll sync: broadcast the editor's top line to every
  // page, which scrolls to the matching `data-line` anchor.
  void sync_clients(int line);

  // Preview -> editor scroll sync. `push_scroll` records the newest line the
  // browser reported; `take_scroll` drains it (returns -1 when nothing new),
  // so the Lua side can apply it on a timer without a callback hop.
  void push_scroll(int line);
  int take_scroll();

private:
  std::unique_ptr<PreviewServerImpl> impl_;
};

#endif // JOT_MARKDOWN_PREVIEW_SERVER_H
