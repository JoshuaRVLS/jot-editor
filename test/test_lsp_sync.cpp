// The document-sync rules between the editor and a language server: what goes
// out on didOpen / didChange / didSave, and which published diagnostics are
// kept. The test seams record the outbound messages instead of writing them to
// a spawned server and feed server messages in through the real parser
// (LSPClient::capture_wire_for_test / receive_for_test).
//
// These rules are the difference between real findings and phantom ones. A
// server's document is only ever updated by didChange -- didSave's `text` is
// informational -- so a save that skips a change leaves the server parsing a
// stale draft and reporting errors at lines the user already fixed. And a
// publish discarded for being a version behind is not necessarily stale: the
// server may have coalesced its parses, making it the freshest word there is.
#include "lsp/client.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace
{
  // A publishDiagnostics message: `version` findings, one error with
  // `message` (or an empty list when `message` is empty).
  std::string publish(const std::string &path, int version, const std::string &message)
  {
    std::string diagnostics = "[]";
    if (!message.empty())
    {
      diagnostics =
          "[{\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},"
          "\"severity\":1,\"message\":\""
          + message + "\"}]";
    }
    return "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{"
           "\"uri\":\""
           + LSPClient::file_uri_from_path(path) + "\",\"version\":" + std::to_string(version)
           + ",\"diagnostics\":" + diagnostics + "}}";
  }

  bool carries(const std::string &message, const std::string &method, const std::string &text)
  {
    return message.find(method) != std::string::npos
           && message.find(text) != std::string::npos;
  }
} // namespace

TEST_CASE("LSP sync: an unchanged didChange is not sent and moves no version", "[lsp][sync]")
{
  LSPClient client("cpp", "/tmp/jot-lsp-sync", {});
  client.capture_wire_for_test();
  const std::string path = "/tmp/jot_lsp_sync_a.cpp";

  REQUIRE(client.did_open(path, "cpp", "int x;\n"));
  REQUIRE(client.captured_wire_for_test().size() == 1);
  REQUIRE(carries(client.captured_wire_for_test()[0], "didOpen", "int x;"));
  REQUIRE(client.document_version_for_test(path) == 1);

  // A duplicate open is just a change with the same text -- nothing to say.
  REQUIRE(client.did_open(path, "cpp", "int x;\n"));
  REQUIRE(client.captured_wire_for_test().size() == 1);

  // The request paths flush the document before every request; when nothing
  // changed, that flush must cost nothing.
  REQUIRE(client.did_change(path, "int x;\n"));
  REQUIRE(client.captured_wire_for_test().size() == 1);
  REQUIRE(client.document_version_for_test(path) == 1);

  REQUIRE(client.did_change(path, "int x = 1;\n"));
  REQUIRE(client.captured_wire_for_test().size() == 2);
  REQUIRE(carries(client.captured_wire_for_test()[1], "didChange", "int x = 1;"));
  REQUIRE(client.document_version_for_test(path) == 2);
}

TEST_CASE("LSP sync: a save sends the edits the change debounce had not", "[lsp][sync]")
{
  LSPClient client("cpp", "/tmp/jot-lsp-sync", {});
  client.capture_wire_for_test();
  const std::string path = "/tmp/jot_lsp_sync_b.cpp";

  REQUIRE(client.did_open(path, "cpp", "int x;\n"));

  // The debounce had not fired when the user hit save. didSave's text field is
  // informational (servers honor it only with save.includeText), so the edit
  // has to travel as a didChange first or the server keeps parsing the old
  // draft and reports a missing `;` the buffer plainly has.
  REQUIRE(client.did_save(path, "int x = 1;\n"));
  const auto wire = client.captured_wire_for_test();
  REQUIRE(wire.size() == 3);
  REQUIRE(carries(wire[1], "didChange", "int x = 1;"));
  REQUIRE(carries(wire[2], "didSave", ""));
  REQUIRE(wire[2].find("didChange") == std::string::npos);
  REQUIRE(client.document_version_for_test(path) == 2);

  // A second save with no edits since sends didSave and nothing else.
  REQUIRE(client.did_save(path, "int x = 1;\n"));
  REQUIRE(client.captured_wire_for_test().size() == 4);
}

TEST_CASE("LSP sync: a publish behind the document is still the freshest word",
          "[lsp][sync]")
{
  LSPClient client("cpp", "/tmp/jot-lsp-sync", {});
  client.capture_wire_for_test();
  const std::string path = "/tmp/jot_lsp_sync_c.cpp";

  REQUIRE(client.did_open(path, "cpp", "int x = 1\n"));
  REQUIRE(client.did_change(path, "int x = 1;\n")); // fixed before the server caught up

  // The server's findings were computed from the version-1 draft and arrive
  // after our second edit: one version behind the document, but the only word
  // the server has given. Discarding it -- the old exact-match rule -- strands
  // whatever it carried on screen with nothing left to replace it.
  client.receive_for_test(publish(path, 1, "expected ';'"));
  auto published = client.consume_published_diagnostics();
  REQUIRE(published.size() == 1);
  REQUIRE(published[0].second.size() == 1);
  REQUIRE(published[0].second[0].message == "expected ';'");

  // A clearing publish for the current version wipes the finding.
  client.receive_for_test(publish(path, 2, ""));
  published = client.consume_published_diagnostics();
  REQUIRE(published.size() == 1);
  REQUIRE(published[0].second.empty());

  // A publish older than the one on screen is an out-of-order duplicate and
  // must not resurrect the error; a re-publish of the applied version (servers
  // publish again after an async parse) still lands.
  client.receive_for_test(publish(path, 1, "expected ';'"));
  REQUIRE(client.consume_published_diagnostics().empty());
  client.receive_for_test(publish(path, 2, "other"));
  published = client.consume_published_diagnostics();
  REQUIRE(published.size() == 1);
  REQUIRE(published[0].second.size() == 1);
  REQUIRE(published[0].second[0].message == "other");
}

TEST_CASE("LSP sync: a reopened file starts its diagnostics bookkeeping fresh",
          "[lsp][sync]")
{
  LSPClient client("cpp", "/tmp/jot-lsp-sync", {});
  client.capture_wire_for_test();
  const std::string path = "/tmp/jot_lsp_sync_d.cpp";

  REQUIRE(client.did_open(path, "cpp", "int x;\n"));
  client.receive_for_test(publish(path, 7, "from the last session"));
  REQUIRE(client.consume_published_diagnostics().size() == 1);

  REQUIRE(client.did_close(path));
  REQUIRE(client.did_open(path, "cpp", "int x;\n")); // a real didOpen again: version 1
  REQUIRE(client.document_version_for_test(path) == 1);

  client.receive_for_test(publish(path, 1, "fresh"));
  auto published = client.consume_published_diagnostics();
  REQUIRE(published.size() == 1);
  REQUIRE(published[0].second.size() == 1);
  REQUIRE(published[0].second[0].message == "fresh");
}
