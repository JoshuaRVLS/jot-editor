// libuv-backed markdown preview HTTP/SSE server (see preview_server.h).
#include "markdown/preview_server.h"

#include "event_loop.h"
#include "markdown/http_message.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <uv.h>

namespace
{
  constexpr size_t kMaxBodyBytes = 1u << 20;      // 1 MiB request bodies
  constexpr size_t kMaxImageBytes = 64u << 20;    // 64 MiB local images
  constexpr int kListenBacklog = 16;

  // A libuv write request owns its payload until the write callback fires:
  // uv_write does not copy the buffer, so the bytes have to outlive the call.
  struct WriteReq
  {
    uv_write_t req;
    std::string data;
  };

  std::string read_file_limited(const std::string &path, bool *ok, long long *size_out)
  {
    *ok = false;
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
      return {};
    if (std::fseek(f, 0, SEEK_END) != 0)
    {
      std::fclose(f);
      return {};
    }
    const long long size = std::ftell(f);
    if (size < 0 || static_cast<size_t>(size) > kMaxImageBytes)
    {
      std::fclose(f);
      return {};
    }
    std::rewind(f);
    std::string out(static_cast<size_t>(size), '\0');
    const size_t read = size > 0 ? std::fread(&out[0], 1, out.size(), f) : 0;
    std::fclose(f);
    out.resize(read);
    *ok = true;
    if (size_out)
      *size_out = static_cast<long long>(out.size());
    return out;
  }
} // namespace

struct PreviewServerImpl
{
  struct Client
  {
    PreviewServerImpl *owner = nullptr;
    uv_tcp_t tcp{};
    std::string inbox;
    char read_buf[16 * 1024]{};
    std::deque<std::string> out_queue;
    bool writing = false;
    bool closing = false;
    bool streaming = false; // an EventSource ("/events") connection
    bool close_after_write = false;
  };

  EventLoop *loop = nullptr;
  uv_loop_t *uv = nullptr;
  uv_tcp_t server{};
  bool has_server = false;
  bool listening = false;
  bool stopping = false;
  int port = 0;
  std::string host;
  std::string page;
  std::string content;
  std::vector<Client *> clients;
  int pending_scroll = -1;

  // ── writes ────────────────────────────────────────────────────────────────
  static void on_write(uv_write_t *req, int status)
  {
    auto *write = reinterpret_cast<WriteReq *>(req);
    Client *client = static_cast<Client *>(req->data);
    delete write;
    if (!client)
      return;
    client->writing = false;
    if (status < 0)
    {
      close_client(client);
      return;
    }
    pump(client);
  }

  static void pump(Client *client)
  {
    if (!client || client->closing || client->writing)
      return;
    if (client->out_queue.empty())
    {
      if (client->close_after_write)
        close_client(client);
      return;
    }
    client->writing = true;
    auto *write = new WriteReq();
    write->data = std::move(client->out_queue.front());
    client->out_queue.pop_front();
    write->req.data = client;
    uv_buf_t buf = uv_buf_init(&write->data[0], static_cast<unsigned>(write->data.size()));
    const int rc = uv_write(&write->req,
                            reinterpret_cast<uv_stream_t *>(&client->tcp),
                            &buf,
                            1,
                            on_write);
    if (rc != 0)
    {
      client->writing = false;
      delete write;
      close_client(client);
    }
  }

  static void send(Client *client, std::string data)
  {
    if (!client || client->closing)
      return;
    client->out_queue.push_back(std::move(data));
    pump(client);
  }

  static void close_client(Client *client)
  {
    if (!client || client->closing)
      return;
    client->closing = true;
    // Drop it from the registry right away so status/client counts and
    // broadcasts never see a dying connection; the object itself lives until
    // libuv runs the close callback.
    if (client->owner)
    {
      auto &list = client->owner->clients;
      list.erase(std::remove(list.begin(), list.end(), client), list.end());
    }
    // A streaming client may still have an in-flight write; cancelling it is
    // fine because the browser reconnects on its own.
    uv_read_stop(reinterpret_cast<uv_stream_t *>(&client->tcp));
    uv_close(reinterpret_cast<uv_handle_t *>(&client->tcp), on_client_closed);
  }

  static void on_client_closed(uv_handle_t *handle)
  {
    auto *client = static_cast<Client *>(handle->data);
    delete client;
  }

  // ── requests ──────────────────────────────────────────────────────────────
  static void respond(Client *client,
                      int status,
                      const std::string &content_type,
                      const std::string &body)
  {
    send(client, jot_http::build_response(status, content_type, body, false));
    client->close_after_write = true;
    pump(client);
  }

  void handle_events(Client *client)
  {
    // Server-Sent Events stream: never closes on its own, carries the page
    // content and editor scroll position to the browser.
    std::string head = "HTTP/1.1 200 OK\r\n"
                       "Content-Type: text/event-stream\r\n"
                       "Cache-Control: no-cache, no-store\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Connection: keep-alive\r\n"
                       "X-Accel-Buffering: no\r\n\r\n";
    head += "retry: 1000\n\n";
    client->streaming = true;
    send(client, std::move(head));

    // Push the current document body immediately so a reconnect re-syncs at
    // once (and so the page works even when it missed the initial push).
    notify_client(client, "content", content);
  }

  static void notify_client(Client *client, const std::string &event, const std::string &data)
  {
    send(client, jot_http::sse_frame(event, data));
  }

  void handle_image(Client *client, const jot_http::Request &req)
  {
    const auto it = req.query.find("path");
    if (it == req.query.end() || it->second.empty())
    {
      respond(client, 400, "text/plain", "missing path");
      return;
    }
    bool ok = false;
    const std::string bytes = read_file_limited(it->second, &ok, nullptr);
    if (!ok)
    {
      respond(client, 404, "text/plain", "not found");
      return;
    }
    respond(client, 200, jot_http::mime_type_for(it->second), bytes);
  }

  void handle_sync(Client *client, const jot_http::Request &req)
  {
    int line = -1;
    if (!req.body.empty())
    {
      // Accepts {"line":N} without pulling in a JSON parser.
      const size_t pos = req.body.find("\"line\"");
      if (pos != std::string::npos)
      {
        const size_t colon = req.body.find(':', pos);
        if (colon != std::string::npos)
          line = std::atoi(req.body.c_str() + colon + 1);
      }
    }
    else
    {
      const auto it = req.query.find("line");
      if (it != req.query.end())
        line = std::atoi(it->second.c_str());
    }
    if (line > 0)
      pending_scroll = line;
    respond(client, 204, "", "");
  }

  void handle_request(Client *client, const jot_http::Request &req)
  {
    const bool is_get = req.method == "GET" || req.method == "HEAD";
    if (req.path == "/events")
    {
      handle_events(client);
      return;
    }
    if (req.path == "/sync" && (is_get || req.method == "POST"))
    {
      handle_sync(client, req);
      return;
    }
    if (req.path == "/image" && is_get)
    {
      handle_image(client, req);
      return;
    }
    if (req.path == "/favicon.ico")
    {
      respond(client, 204, "", "");
      return;
    }
    if (req.path == "/" || req.path == "/index.html")
    {
      respond(client, 200, "text/html; charset=utf-8", page);
      return;
    }
    respond(client, 404, "text/plain", "not found");
  }

  static void process_inbox(Client *client)
  {
    for (;;)
    {
      jot_http::Request req;
      size_t consumed = 0;
      const int rc = jot_http::parse_request(client->inbox, kMaxBodyBytes, &req, &consumed);
      if (rc == 0)
        return;
      if (rc < 0)
      {
        respond(client, 400, "text/plain", "bad request");
        return;
      }
      client->inbox.erase(0, consumed);
      client->owner->handle_request(client, req);
      if (client->closing)
        return;
    }
  }

  // ── connections ───────────────────────────────────────────────────────────
  static void on_alloc(uv_handle_t *handle, size_t /*suggested*/, uv_buf_t *buf)
  {
    auto *client = static_cast<Client *>(handle->data);
    buf->base = client->read_buf;
    buf->len = sizeof(client->read_buf);
  }

  static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
  {
    auto *client = static_cast<Client *>(stream->data);
    if (!client)
      return;
    if (nread < 0)
    {
      close_client(client);
      return;
    }
    if (nread == 0)
      return;
    client->inbox.append(buf->base, static_cast<size_t>(nread));
    process_inbox(client);
  }

  static void on_connection(uv_stream_t *server, int status)
  {
    if (status < 0)
      return;
    auto *impl = static_cast<PreviewServerImpl *>(server->data);
    if (!impl || impl->stopping)
      return;
    auto *client = new Client();
    client->owner = impl;
    uv_tcp_init(impl->uv, &client->tcp);
    client->tcp.data = client;
    if (uv_accept(server, reinterpret_cast<uv_stream_t *>(&client->tcp)) != 0)
    {
      uv_close(reinterpret_cast<uv_handle_t *>(&client->tcp), on_client_closed);
      return;
    }
    uv_tcp_nodelay(&client->tcp, 1);
    impl->clients.push_back(client);
    uv_read_start(reinterpret_cast<uv_stream_t *>(&client->tcp), on_alloc, on_read);
  }
};

PreviewServer::PreviewServer(EventLoop *loop) : impl_(new PreviewServerImpl())
{
  impl_->loop = loop;
}

PreviewServer::~PreviewServer()
{
  stop();
}

bool PreviewServer::start(const std::string &host, int port, int *out_port, std::string *error)
{
  if (impl_->listening)
  {
    if (out_port)
      *out_port = impl_->port;
    return true;
  }
  impl_->loop->assert_main_thread();
  impl_->loop->prepare();
  impl_->uv = impl_->loop->raw_loop();
  if (!impl_->uv)
  {
    if (error)
      *error = "event loop unavailable";
    return false;
  }

  std::string bind_host = host.empty() ? "127.0.0.1" : host;
  if (bind_host == "localhost")
    bind_host = "127.0.0.1";
  const int bind_port = port < 0 ? 0 : port;

  if (uv_tcp_init(impl_->uv, &impl_->server) != 0)
  {
    if (error)
      *error = "uv_tcp_init failed";
    return false;
  }
  impl_->has_server = true;
  impl_->server.data = impl_.get();

  sockaddr_in addr{};
  if (uv_ip4_addr(bind_host.c_str(), bind_port, &addr) != 0)
  {
    if (error)
      *error = "invalid bind address: " + bind_host;
    uv_close(reinterpret_cast<uv_handle_t *>(&impl_->server), nullptr);
    impl_->has_server = false;
    return false;
  }
  if (uv_tcp_bind(&impl_->server, reinterpret_cast<const sockaddr *>(&addr), 0) != 0)
  {
    if (error)
      *error = "cannot bind " + bind_host + ":" + std::to_string(bind_port);
    uv_close(reinterpret_cast<uv_handle_t *>(&impl_->server), nullptr);
    impl_->has_server = false;
    return false;
  }
  if (uv_listen(reinterpret_cast<uv_stream_t *>(&impl_->server), kListenBacklog, PreviewServerImpl::on_connection) != 0)
  {
    if (error)
      *error = "cannot listen on " + bind_host + ":" + std::to_string(bind_port);
    uv_close(reinterpret_cast<uv_handle_t *>(&impl_->server), nullptr);
    impl_->has_server = false;
    return false;
  }

  sockaddr_storage bound{};
  int len = sizeof(bound);
  if (uv_tcp_getsockname(&impl_->server, reinterpret_cast<sockaddr *>(&bound), &len) == 0)
  {
    if (bound.ss_family == AF_INET)
      impl_->port = ntohs(reinterpret_cast<sockaddr_in *>(&bound)->sin_port);
    else if (bound.ss_family == AF_INET6)
      impl_->port = ntohs(reinterpret_cast<sockaddr_in6 *>(&bound)->sin6_port);
  }
  impl_->host = bind_host;
  impl_->listening = true;
  impl_->stopping = false;
  if (out_port)
    *out_port = impl_->port;
  return true;
}

void PreviewServer::stop()
{
  if (!impl_)
    return;
  impl_->stopping = true;
  for (PreviewServerImpl::Client *client : std::vector<PreviewServerImpl::Client *>(impl_->clients))
    PreviewServerImpl::close_client(client);
  impl_->clients.clear();
  if (impl_->has_server && !uv_is_closing(reinterpret_cast<uv_handle_t *>(&impl_->server)))
  {
    uv_close(reinterpret_cast<uv_handle_t *>(&impl_->server), nullptr);
  }
  impl_->has_server = false;
  impl_->listening = false;
  impl_->port = 0;
  impl_->pending_scroll = -1;
}

bool PreviewServer::running() const
{
  return impl_ && impl_->listening;
}

int PreviewServer::port() const
{
  return impl_ ? impl_->port : 0;
}

int PreviewServer::client_count() const
{
  return impl_ ? static_cast<int>(impl_->clients.size()) : 0;
}

void PreviewServer::set_page(std::string html)
{
  if (impl_)
    impl_->page = std::move(html);
}

const std::string &PreviewServer::page() const
{
  static const std::string kEmpty;
  return impl_ ? impl_->page : kEmpty;
}

void PreviewServer::set_content(std::string body)
{
  if (impl_)
    impl_->content = std::move(body);
}

const std::string &PreviewServer::content() const
{
  static const std::string kEmpty;
  return impl_ ? impl_->content : kEmpty;
}

void PreviewServer::sync_clients(int line)
{
  notify("sync", std::to_string(line));
}

void PreviewServer::notify(const std::string &event, const std::string &data)
{
  if (!impl_)
    return;
  for (PreviewServerImpl::Client *client : std::vector<PreviewServerImpl::Client *>(impl_->clients))
  {
    if (client->streaming)
      PreviewServerImpl::notify_client(client, event, data);
  }
}

void PreviewServer::push_scroll(int line)
{
  if (impl_)
    impl_->pending_scroll = line;
}

int PreviewServer::take_scroll()
{
  if (!impl_)
    return -1;
  const int line = impl_->pending_scroll;
  impl_->pending_scroll = -1;
  return line;
}
