// The tab order model (src/features/tab_order.*).
//
// The strip's order, its pins and its jump-to-buffer letters survive buffer
// indices shifting under them (buffers is a vector erased from the middle), so
// these cases are mostly about identity: a buffer keeps its place when a tab to
// its left closes, a pin survives a neighbour closing, and a letter stays with
// its buffer for the buffer's life. The rest pins the rules the commands rely
// on -- pinned leads the strip, a move never crosses the pin boundary, a sort
// leaves the pinned block alone.
#include "ui/tab_order.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace
{
  FileBuffer file(const std::string &path, bool placeholder = false)
  {
    FileBuffer buffer;
    buffer.filepath = path;
    buffer.lines = {"x"};
    buffer.is_placeholder = placeholder;
    buffer.modified = !placeholder;
    return buffer;
  }

  FileBuffer unnamed(bool modified = true)
  {
    FileBuffer buffer;
    buffer.lines = {""};
    buffer.is_placeholder = true;
    buffer.modified = modified;
    return buffer;
  }

  // Names of the buffers in strip order, for readable assertions.
  std::vector<std::string> names(const TabOrder &order, const std::vector<FileBuffer> &buffers)
  {
    std::vector<std::string> out;
    for (int index : order.indices(buffers))
    {
      std::string name = buffers[(size_t)index].filepath;
      const size_t slash = name.find_last_of('/');
      name = slash == std::string::npos ? name : name.substr(slash + 1);
      out.push_back(name.empty() ? "[No Name]" : name);
    }
    return out;
  }

  std::vector<std::string> expect(std::initializer_list<const char *> list)
  {
    return std::vector<std::string>(list.begin(), list.end());
  }
} // namespace

TEST_CASE("The tab order keeps its places when a buffer closes to the left", "[jot][tabline]")
{
  std::vector<FileBuffer> buffers;
  buffers.push_back(file("/w/a.cpp"));
  buffers.push_back(file("/w/b.cpp"));
  buffers.push_back(file("/w/c.cpp"));
  TabOrder order;
  order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_END);
  REQUIRE(names(order, buffers) == expect({"a.cpp", "b.cpp", "c.cpp"}));

  // Close the first: every index shifts down by one, the order must not.
  const long long c_uid = buffers[2].tab_uid;
  buffers.erase(buffers.begin());
  order.sync(buffers, c_uid, TabInsert::TAB_INSERT_END);
  REQUIRE(names(order, buffers) == expect({"b.cpp", "c.cpp"}));
  REQUIRE(order.position_of(buffers, 1) == 1);
  REQUIRE(buffers[1].tab_uid == c_uid);
}

TEST_CASE("Newcomers land by the insert policy, as a block", "[jot][tabline]")
{
  SECTION("after the current buffer")
  {
    std::vector<FileBuffer> buffers;
  buffers.push_back(file("/w/a.cpp"));
  buffers.push_back(file("/w/b.cpp"));
    TabOrder order;
    order.sync(buffers, 0, TabInsert::TAB_INSERT_END);
    // Two files open at once while the cursor is on a.cpp: both land after it,
    // in the order the buffers came in.
    buffers.push_back(file("/w/x.cpp"));
    buffers.push_back(file("/w/y.cpp"));
    order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_AFTER_CURRENT);
    REQUIRE(names(order, buffers) == expect({"a.cpp", "x.cpp", "y.cpp", "b.cpp"}));
  }

  SECTION("at the start and at the end")
  {
    std::vector<FileBuffer> buffers;
  buffers.push_back(file("/w/a.cpp"));
    TabOrder order;
    order.sync(buffers, 0, TabInsert::TAB_INSERT_END);
    buffers.push_back(file("/w/b.cpp"));
    order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_START);
    REQUIRE(names(order, buffers) == expect({"b.cpp", "a.cpp"}));

    buffers.push_back(file("/w/c.cpp"));
    order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_END);
    REQUIRE(names(order, buffers) == expect({"b.cpp", "a.cpp", "c.cpp"}));
  }
}

TEST_CASE("A placeholder that was never touched is not a tab", "[jot][tabline]")
{
  std::vector<FileBuffer> buffers;
  buffers.push_back(unnamed(false));
  buffers.push_back(file("/w/a.cpp"));
  TabOrder order;
  order.sync(buffers, buffers[1].tab_uid, TabInsert::TAB_INSERT_END);
  REQUIRE(names(order, buffers) == expect({"a.cpp"}));

  // Typing into it (or pointing it at a file) makes it one.
  buffers[0].modified = true;
  REQUIRE(names(order, buffers) == expect({"[No Name]", "a.cpp"}));
}

TEST_CASE("Pinned buffers lead the strip and stay out of the way of sorts", "[jot][tabline]")
{
  std::vector<FileBuffer> buffers;
  buffers.push_back(file("/w/a.cpp"));
  buffers.push_back(file("/w/b.cpp"));
  buffers.push_back(file("/w/c.cpp"));
  TabOrder order;
  order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_END);
  order.toggle_pin(2, buffers);
  REQUIRE(names(order, buffers) == expect({"c.cpp", "a.cpp", "b.cpp"}));
  REQUIRE(order.pinned(2, buffers));

  // A sort orders the unpinned block (here by name) and leaves the pin where it
  // is; c.cpp would sort between a and b, but it is pinned.
  order.sort(buffers, TabSort::TAB_SORT_NAME);
  REQUIRE(names(order, buffers) == expect({"c.cpp", "a.cpp", "b.cpp"}));

  order.toggle_pin(2, buffers);
  REQUIRE_FALSE(order.pinned(2, buffers));
  order.sort(buffers, TabSort::TAB_SORT_NAME);
  REQUIRE(names(order, buffers) == expect({"a.cpp", "b.cpp", "c.cpp"}));
}

TEST_CASE("Moving a tab cannot cross the pin boundary", "[jot][tabline]")
{
  std::vector<FileBuffer> buffers;
  buffers.push_back(file("/w/a.cpp"));
  buffers.push_back(file("/w/b.cpp"));
  buffers.push_back(file("/w/c.cpp"));
  TabOrder order;
  order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_END);
  order.toggle_pin(0, buffers); // a.cpp leads, pinned

  // b moves right past c.
  REQUIRE(order.move(buffers, 1, +1));
  REQUIRE(names(order, buffers) == expect({"a.cpp", "c.cpp", "b.cpp"}));
  // And back to the left, but no further: the pinned buffer is in the way.
  REQUIRE(order.move(buffers, 1, -1));
  REQUIRE(names(order, buffers) == expect({"a.cpp", "b.cpp", "c.cpp"}));
  REQUIRE_FALSE(order.move(buffers, 1, -1));
  // The pinned buffer itself cannot leave its block.
  REQUIRE_FALSE(order.move(buffers, 0, +1));
  REQUIRE_FALSE(order.move(buffers, 0, -1));
}

TEST_CASE("Jump letters are semantic, unique, and stay with the buffer", "[jot][tabline]")
{
  std::vector<FileBuffer> buffers;
  buffers.push_back(file("/w/README.md"));
  buffers.push_back(file("/w/main.cpp"));
  buffers.push_back(file("/w/mesh.cpp"));
  buffers.push_back(file("/w/z.cpp"));
  TabOrder order;
  order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_END);

  REQUIRE(order.letter_for(0, buffers) == 'r');
  REQUIRE(order.letter_for(1, buffers) == 'm'); // main.cpp takes m first
  const char mesh = order.letter_for(2, buffers);
  REQUIRE(mesh != 'm');
  REQUIRE(mesh != 'r');
  // Asking again returns the same letter, however the order was shuffled since.
  order.move(buffers, 2, +1);
  REQUIRE(order.letter_for(2, buffers) == mesh);
  REQUIRE(order.letter_for(0, buffers) == 'r');
}

TEST_CASE("A jump letter reads back as the buffer that owns it", "[jot][tabline]")
{
  std::vector<FileBuffer> buffers;
  buffers.push_back(file("/w/alpha.cpp"));
  buffers.push_back(file("/w/beta.cpp"));
  buffers.push_back(file("/w/gamma.cpp"));
  TabOrder order;
  order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_END);

  // Letters are handed out when they are asked for -- arming the picker asks
  // about every tab -- and from then on the lookup resolves what was painted.
  REQUIRE(order.letter_for(0, buffers) == 'a');
  REQUIRE(order.letter_for(1, buffers) == 'b');
  REQUIRE(order.letter_for(2, buffers) == 'g');
  REQUIRE(order.buffer_for_letter(buffers, 'b') == 1);
  REQUIRE(order.buffer_for_letter(buffers, 'a') == 0);
  // A letter nobody owns answers -1 rather than some neighbouring tab.
  REQUIRE(order.buffer_for_letter(buffers, 'z') == -1);

  // A letter whose buffer closed resolves to nothing, not to whatever buffer
  // took that slot in the vector.
  buffers.erase(buffers.begin() + 1);
  order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_END);
  REQUIRE(order.buffer_for_letter(buffers, 'b') == -1);
  REQUIRE(order.buffer_for_letter(buffers, 'g') == 1);
}

TEST_CASE("A letter goes back into the pool when its buffer closes", "[jot][tabline]")
{
  std::vector<FileBuffer> buffers;
  buffers.push_back(file("/w/alpha.cpp"));
  buffers.push_back(file("/w/beta.cpp"));
  TabOrder order;
  order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_END);
  REQUIRE(order.letter_for(0, buffers) == 'a');
  REQUIRE(order.letter_for(1, buffers) == 'b');

  // alpha closes: its letter is released, beta keeps its own.
  buffers.erase(buffers.begin());
  order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_END);
  REQUIRE(order.letter_for(0, buffers) == 'b');

  // So a file whose initial is `a` can take it back.
  buffers.push_back(file("/w/apple.cpp"));
  order.sync(buffers, buffers[0].tab_uid, TabInsert::TAB_INSERT_END);
  REQUIRE(order.position_of(buffers, 1) >= 0);
  REQUIRE(order.letter_for(1, buffers) == 'a');
}
