// Markdown preview server: a tiny HTTP/1.1 server that serves the rendered
// markdown page to a browser, streams live updates over Server-Sent Events and
// receives scroll positions back from the page (browser -> editor sync).
//
// It is driven by the editor's existing libuv loop (EventLoop), so it adds no
// threads and no sockets of its own: every handle lives on the main loop and
// every callback runs on the main thread, which is also where Lua executes.
//
// Routes:
//   GET  /               the rendered page shell
//   GET  /events         text/event-stream (live updates + scroll sync)
//   POST /sync           body {"line":N} — the browser's top visible line
//   GET  /sync?line=N    same, for clients that cannot POST
//   GET  /image?path=..  a local image referenced by the document
//   GET  /favicon.ico    204
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
