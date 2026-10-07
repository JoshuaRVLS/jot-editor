// Emmet abbreviations: markup and CSS shorthands expanded into snippet text
// (see emmet.h for the two halves and why they are separate).
//
// The markup parser is a small recursive-descent one over a tree of elements:
// `>` descends, `+` is a sibling, `^` climbs back out, and `*n` multiplies. The
// repeat is applied as a second pass rather than while parsing, because Emmet
// multiplies the whole subtree -- `ul>li*3>a` is three list items that each hold
// a link, not one list item with a link plus two empty ones -- and a node's `$`
// numbering has to see the count its own repeat produced.
//
// The CSS side resolves a shorthand against a table of property aliases and
// value keywords, then formats the values: a bare number takes the property's
// default unit, `-` separates the values of a run, and `!` appends `!important`.
//
// Both halves return false rather than guessing. That is the contract the caller
// leans on: a Tab over ordinary prose, a word in a comment, or a JSX expression
// has to fall through to the editor's own indentation, so anything that does not
// parse is left exactly as it was typed.
#include "extras/emmet/expand.h"
#include "base/string_util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
  using Emmet::Expansion;

  bool is_space(char c)
  {
    return c == ' ' || c == '\t';
  }

  // ---------------------------------------------------------------------------
  // Tokenizing
  // ---------------------------------------------------------------------------

  bool markup_token_char(char c)
  {
    switch (c)
    {
    case '>':
    case '+':
    case '^':
    case '*':
    case '$':
    case '.':
    case '#':
    case '[':
    case ']':
    case '{':
    case '}':
    case '(':
    case ')':
    case '/':
    case '!':
    case '=':
    case '"':
    case '\'':
      return true;
    default:
      return std::isalnum((unsigned char)c) != 0 || c == '-' || c == '_' || c == ':';
    }
  }

  bool css_token_char(char c)
  {
    switch (c)
    {
    case '#':
    case '%':
    case '+':
    case '!':
    case '.':
    case ',':
    case '/':
    case '"':
    case '\'':
    case '(':
    case ')':
      return true;
    default:
      return std::isalnum((unsigned char)c) != 0 || c == '-' || c == '_';
    }
  }

  // Where an abbreviation may start. The token itself stops at whitespace, and
  // what came *before* it decides whether a Tab could have meant one: expanding
  // after `<` or `/` would make `<<div></div>` or `</<div>`, and expanding inside
  // an expression is never what a Tab meant.
  bool boundary_before(const std::string &line, int token_start)
  {
    if (token_start <= 0)
      return true;
    const char prev = line[(size_t)token_start - 1];
    if (is_space(prev))
      return true;
    switch (prev)
    {
    case '>':
    case '{':
    case '(':
    case ',':
    case ';':
    case '|':
    case '=':
      return true;
    default:
      return false;
    }
  }

  // ---------------------------------------------------------------------------
  // Markup
  // ---------------------------------------------------------------------------

  bool ident_start(char c)
  {
    return std::isalpha((unsigned char)c) != 0 || c == '_';
  }

  bool ident_char(char c)
  {
    return std::isalnum((unsigned char)c) != 0 || c == '-' || c == '_' || c == '$' || c == ':';
  }

  const std::vector<std::string> &void_elements()
  {
    static const std::vector<std::string> tags = {"area",
                                                  "base",
                                                  "br",
                                                  "col",
                                                  "embed",
                                                  "hr",
                                                  "img",
                                                  "input",
                                                  "link",
                                                  "meta",
                                                  "param",
                                                  "source",
                                                  "track",
                                                  "wbr"};
    return tags;
  }

  bool is_void_element(const std::string &name)
  {
    const std::string lower = string_util::lower_copy(name);
    const auto &tags = void_elements();
    return std::find(tags.begin(), tags.end(), lower) != tags.end();
  }

  // The attributes Emmet fills in for an element the author did not describe --
  // `a` is a link, `img` needs a source. Only added when the author left them
  // out, so `a[href=#]` keeps the href they wrote.
  const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> &
  implicit_attributes()
  {
    static const std::vector<
        std::pair<std::string, std::vector<std::pair<std::string, std::string>>>>
        table = {
            {"a", {{"href", ""}}},
            {"area", {{"shape", ""}, {"coords", ""}, {"href", ""}, {"alt", ""}}},
            {"abbr", {{"title", ""}}},
            {"base", {{"href", ""}}},
            {"blockquote", {{"cite", ""}}},
            {"button", {{"type", "button"}}},
            {"del", {{"cite", ""}}},
            {"form", {{"action", ""}}},
            {"iframe", {{"src", ""}}},
            {"img", {{"src", ""}, {"alt", ""}}},
            {"input", {{"type", "text"}}},
            {"ins", {{"cite", ""}}},
            {"link", {{"rel", "stylesheet"}, {"href", ""}}},
            {"object", {{"data", ""}}},
            {"q", {{"cite", ""}}},
            {"script", {{"src", ""}}},
            {"select", {{"name", ""}, {"id", ""}}},
            {"audio", {{"src", ""}}},
            {"video", {{"src", ""}}},
            {"textarea", {{"name", ""}, {"id", ""}}},
            {"time", {{"datetime", ""}}},
            {"td", {{"colspan", ""}, {"rowspan", ""}}},
            {"th", {{"colspan", ""}, {"rowspan", ""}}},
    };
    return table;
  }

  struct Node
  {
    std::string name;
    std::string id;
    std::vector<std::string> classes;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;
    bool has_text = false;
    bool self_close = false;
    int repeat = 1; // `*n`, resolved into copies before rendering
    int index = 0;  // the `$` number this node was born with (0 = inherit)
    std::vector<Node> children;
  };

  // A `$` in a name, class, attribute or text is the number of the repeat that
  // encloses it.
  std::string number_with(std::string value, int index)
  {
    const std::string replacement = std::to_string(index > 0 ? index : 1);
    size_t at = 0;
    while ((at = value.find('$', at)) != std::string::npos)
    {
      value.replace(at, 1, replacement);
      at += replacement.size();
    }
    return value;
  }

  class MarkupParser
  {
  public:
    explicit MarkupParser(const std::string &text) : text_(text)
    {
    }

    bool run(std::vector<Node> &out)
    {
      Node root;
      if (!items(root))
        return false;
      if (pos_ != text_.size() || root.children.empty())
        return false;
      out = std::move(root.children);
      return true;
    }

  private:
    bool items(Node &root)
    {
      std::vector<Node *> stack = {&root};
      bool any = false;
      // An operator with nothing to its right (`div>`, `div+`, a trailing `^`) is
      // not an abbreviation, and a Tab over one has to fall through.
      bool expecting_item = false;
      while (pos_ < text_.size())
      {
        const char c = text_[pos_];
        if (c == '>')
        {
          if (stack.back()->children.empty())
            return false;
          // Descend into the item just added.
          stack.push_back(&stack.back()->children.back());
          expecting_item = true;
          pos_++;
          continue;
        }
        if (c == '+')
        {
          expecting_item = true;
          pos_++;
          continue;
        }
        if (c == '^')
        {
          if (stack.size() <= 1)
            return false;
          stack.pop_back();
          expecting_item = false;
          pos_++;
          continue;
        }
        if (c == ')' || c == '(' || c == ']' || c == '}')
          return false;

        Node node;
        if (!item(node))
          return false;
        stack.back()->children.push_back(std::move(node));
        any = true;
        expecting_item = false;
      }
      return any && !expecting_item;
    }

    bool item(Node &node)
    {
      if (pos_ < text_.size() && ident_start(text_[pos_]))
      {
        const size_t start = pos_;
        while (pos_ < text_.size() && ident_char(text_[pos_]))
          pos_++;
        node.name = text_.substr(start, pos_ - start);
      }

      bool described = !node.name.empty();
      while (pos_ < text_.size())
      {
        const char c = text_[pos_];
        if (c == '#' || c == '.')
        {
          pos_++;
          const size_t from = pos_;
          while (pos_ < text_.size() && ident_char(text_[pos_]))
            pos_++;
          if (pos_ == from)
            return false;
          const std::string value = text_.substr(from, pos_ - from);
          if (c == '#')
            node.id = value;
          else
            node.classes.push_back(value);
          described = true;
          continue;
        }
        if (c == '[')
        {
          if (!attributes(node))
            return false;
          described = true;
          continue;
        }
        if (c == '{')
        {
          if (!braced_text(node.text))
            return false;
          node.has_text = true;
          described = true;
          continue;
        }
        if (c == '*')
        {
          pos_++;
          const size_t from = pos_;
          while (pos_ < text_.size() && std::isdigit((unsigned char)text_[pos_]))
            pos_++;
          if (pos_ == from)
            return false;
          const int count = std::atoi(text_.substr(from, pos_ - from).c_str());
          if (count <= 0 || count > 1000)
            return false;
          node.repeat = count;
          described = true;
          continue;
        }
        if (c == '/')
        {
          node.self_close = true;
          pos_++;
          described = true;
          continue;
        }
        break;
      }
      return described;
    }

    // A `{...}` body, with nesting so `{a{b}c}` is one text.
    bool braced_text(std::string &out)
    {
      if (text_[pos_] != '{')
        return false;
      const size_t start = ++pos_;
      int depth = 1;
      while (pos_ < text_.size())
      {
        if (text_[pos_] == '{')
          depth++;
        else if (text_[pos_] == '}')
        {
          depth--;
          if (depth == 0)
          {
            out = text_.substr(start, pos_ - start);
            pos_++;
            return true;
          }
        }
        pos_++;
      }
      return false;
    }

    bool attributes(Node &node)
    {
      pos_++; // '['
      while (pos_ < text_.size())
      {
        while (pos_ < text_.size() && is_space(text_[pos_]))
          pos_++;
        if (pos_ < text_.size() && text_[pos_] == ']')
        {
          pos_++;
          return true;
        }
        const size_t from = pos_;
        while (pos_ < text_.size() && text_[pos_] != '=' && text_[pos_] != ']'
               && !is_space(text_[pos_]))
          pos_++;
        if (pos_ == from)
          return false;
        std::string name = text_.substr(from, pos_ - from);
        std::string value;
        if (pos_ < text_.size() && text_[pos_] == '=')
        {
          pos_++;
          if (pos_ < text_.size() && (text_[pos_] == '"' || text_[pos_] == '\''))
          {
            const char quote = text_[pos_++];
            const size_t value_start = pos_;
            while (pos_ < text_.size() && text_[pos_] != quote)
              pos_++;
            if (pos_ >= text_.size())
              return false;
            value = text_.substr(value_start, pos_ - value_start);
            pos_++;
          }
          else
          {
            const size_t value_start = pos_;
            while (pos_ < text_.size() && !is_space(text_[pos_]) && text_[pos_] != ']')
              pos_++;
            value = text_.substr(value_start, pos_ - value_start);
          }
        }
        node.attrs.emplace_back(std::move(name), std::move(value));
      }
      return false;
    }

    const std::string &text_;
    size_t pos_ = 0;
  };

  // `*n` becomes n copies of the node, each holding the same children, with the
  // numbering handed down so a `$` in a child resolves against the repeat that
  // encloses it.
  void expand_repeats(Node &node, int outer_index)
  {
    std::vector<Node> expanded;
    expanded.reserve(node.children.size());
    for (Node &child : node.children)
    {
      const int count = std::max(1, child.repeat);
      for (int i = 1; i <= count; i++)
      {
        Node copy = child;
        copy.repeat = 1;
        copy.index = count > 1 ? i : 0;
        const int scope = copy.index > 0 ? copy.index : outer_index;
        if (!copy.children.empty())
          expand_repeats(copy, scope);
        expanded.push_back(std::move(copy));
      }
    }
    node.children = std::move(expanded);
  }

  // The empty leaf the caret lands in: the last one in document order, so
  // `div>p` puts it inside the paragraph and `ul>li*3` inside the third item.
  const Node *caret_leaf(const std::vector<Node> &nodes)
  {
    const Node *found = nullptr;
    for (const Node &node : nodes)
    {
      if (!node.children.empty())
      {
        if (const Node *deeper = caret_leaf(node.children))
          found = deeper;
        continue;
      }
      const std::string name = node.name.empty() ? "div" : node.name;
      if (node.has_text || node.self_close || is_void_element(name))
        continue;
      found = &node;
    }
    return found;
  }

  struct MarkupOptions
  {
    std::string indent;
    bool jsx = false;
  };

  void render_node(const Node &node,
                   int depth,
                   int scope_index,
                   const MarkupOptions &opts,
                   const Node *caret,
                   std::string &out)
  {
    const int index = node.index > 0 ? node.index : scope_index;
    const std::string name = number_with(node.name.empty() ? "div" : node.name, index);

    std::string start;
    for (int i = 0; i < depth; i++)
      start += opts.indent;
    start += "<" + name;

    if (!node.id.empty())
      start += " id=\"" + number_with(node.id, index) + "\"";
    if (!node.classes.empty())
    {
      std::string joined;
      for (size_t i = 0; i < node.classes.size(); i++)
      {
        if (i)
          joined += " ";
        joined += number_with(node.classes[i], index);
      }
      start += " class=\"" + joined + "\"";
    }
    for (const auto &attr : node.attrs)
    {
      start += " " + attr.first + "=\"";
      start += number_with(attr.second, index) + "\"";
    }

    // The element's own defaults, for the attributes the author did not name --
    // but only if the description did not already cover them.
    for (const auto &entry : implicit_attributes())
    {
      if (entry.first != name)
        continue;
      for (const auto &pair : entry.second)
      {
        const bool named = std::any_of(node.attrs.begin(),
                                       node.attrs.end(),
                                       [&](const std::pair<std::string, std::string> &attr)
                                       { return attr.first == pair.first; });
        if (named)
          continue;
        if (pair.first == "id" && !node.id.empty())
          continue;
        if (pair.first == "class" && !node.classes.empty())
          continue;
        start += " " + pair.first + "=\"" + pair.second + "\"";
      }
      break;
    }

    if (node.self_close || is_void_element(name))
    {
      out += start + (opts.jsx ? " />" : ">");
      return;
    }

    out += start + ">";
    if (node.has_text)
    {
      out += number_with(node.text, index) + "</" + name + ">";
      return;
    }
    if (node.children.empty())
    {
      if (&node == caret)
        out += "$0";
      out += "</" + name + ">";
      return;
    }
    out += "\n";
    for (size_t i = 0; i < node.children.size(); i++)
    {
      if (i)
        out += "\n";
      render_node(node.children[i], depth + 1, index, opts, caret, out);
    }
    out += "\n";
    for (int i = 0; i < depth; i++)
      out += opts.indent;
    out += "</" + name + ">";
  }

  // ---------------------------------------------------------------------------
  // CSS
  // ---------------------------------------------------------------------------

  struct Alias
  {
    const char *short_form;
    const char *property;
  };

  const std::vector<Alias> &css_aliases()
  {
    static const std::vector<Alias> table = {
        {"ac", "align-content"},
        {"ai", "align-items"},
        {"as", "align-self"},
        {"anim", "animation"},
        {"bg", "background"},
        {"bgc", "background-color"},
        {"bgi", "background-image"},
        {"bgp", "background-position"},
        {"bgr", "background-repeat"},
        {"bgs", "background-size"},
        {"b", "bottom"},
        {"bd", "border"},
        {"bdc", "border-color"},
        {"bdl", "border-left"},
        {"bdr", "border-radius"},
        {"bds", "border-style"},
        {"bdw", "border-width"},
        {"bxsh", "box-shadow"},
        {"bxz", "box-sizing"},
        {"c", "color"},
        {"cl", "clear"},
        {"cnt", "content"},
        {"cur", "cursor"},
        {"d", "display"},
        {"fx", "flex"},
        {"fxd", "flex-direction"},
        {"fxg", "flex-grow"},
        {"fxs", "flex-shrink"},
        {"fxw", "flex-wrap"},
        {"fl", "float"},
        {"ff", "font-family"},
        {"fs", "font-style"},
        {"fw", "font-weight"},
        {"fz", "font-size"},
        {"g", "gap"},
        {"gap", "gap"},
        {"h", "height"},
        {"jc", "justify-content"},
        {"l", "left"},
        {"lis", "list-style"},
        {"lh", "line-height"},
        {"lts", "letter-spacing"},
        {"m", "margin"},
        {"mah", "max-height"},
        {"maw", "max-width"},
        {"mb", "margin-bottom"},
        {"mih", "min-height"},
        {"miw", "min-width"},
        {"ml", "margin-left"},
        {"mr", "margin-right"},
        {"mt", "margin-top"},
        {"of", "object-fit"},
        {"op", "opacity"},
        {"ord", "order"},
        {"ov", "overflow"},
        {"ovx", "overflow-x"},
        {"ovy", "overflow-y"},
        {"p", "padding"},
        {"pb", "padding-bottom"},
        {"pe", "pointer-events"},
        {"pl", "padding-left"},
        {"pos", "position"},
        {"pr", "padding-right"},
        {"pt", "padding-top"},
        {"r", "right"},
        {"t", "top"},
        {"ta", "text-align"},
        {"td", "text-decoration"},
        {"trf", "transform"},
        {"trs", "transition"},
        {"tt", "text-transform"},
        {"us", "user-select"},
        {"v", "visibility"},
        {"va", "vertical-align"},
        {"w", "width"},
        {"ws", "white-space"},
        {"z", "z-index"},
        // Full property names, so `margin`, `margin10` and `display:f` all read.
        {"align-items", "align-items"},
        {"background", "background"},
        {"border", "border"},
        {"box-shadow", "box-shadow"},
        {"color", "color"},
        {"content", "content"},
        {"cursor", "cursor"},
        {"display", "display"},
        {"font-size", "font-size"},
        {"font-weight", "font-weight"},
        {"height", "height"},
        {"justify-content", "justify-content"},
        {"letter-spacing", "letter-spacing"},
        {"line-height", "line-height"},
        {"margin", "margin"},
        {"opacity", "opacity"},
        {"overflow", "overflow"},
        {"padding", "padding"},
        {"position", "position"},
        {"text-align", "text-align"},
        {"vertical-align", "vertical-align"},
        {"white-space", "white-space"},
        {"width", "width"},
        {"z-index", "z-index"},
    };
    return table;
  }

  const std::map<std::string, std::string> *css_keywords(const std::string &property)
  {
    static const std::map<std::string, std::map<std::string, std::string>> table = {
        {"display",
         {{"n", "none"},
          {"b", "block"},
          {"c", "contents"},
          {"f", "flex"},
          {"g", "grid"},
          {"i", "inline"},
          {"ib", "inline-block"},
          {"if", "inline-flex"},
          {"ig", "inline-grid"},
          {"li", "list-item"},
          {"t", "table"},
          {"tc", "table-cell"}}},
        {"position",
         {{"a", "absolute"}, {"f", "fixed"}, {"r", "relative"}, {"s", "static"}, {"st", "sticky"}}},
        {"text-align",
         {{"c", "center"},
          {"e", "end"},
          {"j", "justify"},
          {"l", "left"},
          {"r", "right"},
          {"s", "start"}}},
        {"overflow", {{"a", "auto"}, {"h", "hidden"}, {"s", "scroll"}, {"v", "visible"}}},
        {"overflow-x", {{"a", "auto"}, {"h", "hidden"}, {"s", "scroll"}, {"v", "visible"}}},
        {"overflow-y", {{"a", "auto"}, {"h", "hidden"}, {"s", "scroll"}, {"v", "visible"}}},
        {"float", {{"l", "left"}, {"n", "none"}, {"r", "right"}}},
        {"clear", {{"b", "both"}, {"l", "left"}, {"n", "none"}, {"r", "right"}}},
        {"align-items",
         {{"b", "baseline"},
          {"c", "center"},
          {"fe", "flex-end"},
          {"fs", "flex-start"},
          {"s", "stretch"}}},
        {"align-self",
         {{"a", "auto"},
          {"c", "center"},
          {"fe", "flex-end"},
          {"fs", "flex-start"},
          {"s", "stretch"}}},
        {"align-content",
         {{"c", "center"}, {"sa", "space-around"}, {"sb", "space-between"}, {"s", "stretch"}}},
        {"justify-content",
         {{"c", "center"},
          {"fe", "flex-end"},
          {"fs", "flex-start"},
          {"sa", "space-around"},
          {"sb", "space-between"},
          {"se", "space-evenly"}}},
        {"flex-direction",
         {{"c", "column"}, {"cr", "column-reverse"}, {"r", "row"}, {"rr", "row-reverse"}}},
        {"flex-wrap", {{"nw", "nowrap"}, {"w", "wrap"}, {"wr", "wrap-reverse"}}},
        {"font-weight", {{"b", "bold"}, {"br", "bolder"}, {"l", "lighter"}, {"n", "normal"}}},
        {"font-style", {{"i", "italic"}, {"n", "normal"}, {"o", "oblique"}}},
        {"text-decoration",
         {{"lt", "line-through"}, {"n", "none"}, {"o", "overline"}, {"u", "underline"}}},
        {"text-transform",
         {{"c", "capitalize"}, {"l", "lowercase"}, {"n", "none"}, {"u", "uppercase"}}},
        {"cursor",
         {{"c", "crosshair"},
          {"d", "default"},
          {"g", "grab"},
          {"h", "help"},
          {"m", "move"},
          {"na", "not-allowed"},
          {"p", "pointer"},
          {"t", "text"},
          {"w", "wait"}}},
        {"visibility", {{"c", "collapse"}, {"h", "hidden"}, {"v", "visible"}}},
        {"white-space",
         {{"n", "nowrap"}, {"nm", "normal"}, {"p", "pre"}, {"pl", "pre-line"}, {"pw", "pre-wrap"}}},
        {"border", {{"d", "dashed"}, {"dt", "dotted"}, {"n", "none"}, {"s", "solid"}}},
        {"border-style", {{"d", "dashed"}, {"dt", "dotted"}, {"n", "none"}, {"s", "solid"}}},
        {"box-sizing", {{"bb", "border-box"}, {"cb", "content-box"}}},
        {"object-fit", {{"c", "cover"}, {"ct", "contain"}, {"f", "fill"}, {"sd", "scale-down"}}},
        {"pointer-events", {{"a", "auto"}, {"n", "none"}}},
        {"user-select", {{"a", "all"}, {"n", "none"}, {"t", "text"}}},
        {"list-style", {{"n", "none"}}},
        {"background-repeat",
         {{"nr", "no-repeat"}, {"r", "repeat"}, {"rx", "repeat-x"}, {"ry", "repeat-y"}}},
        {"background-size", {{"a", "auto"}, {"c", "cover"}, {"ct", "contain"}}},
        {"vertical-align", {{"b", "bottom"}, {"bl", "baseline"}, {"m", "middle"}, {"t", "top"}}},
        {"flex", {{"a", "auto"}, {"i", "initial"}, {"n", "none"}}},
    };
    const auto found = table.find(property);
    return found == table.end() ? nullptr : &found->second;
  }

  // Properties whose bare number is not a length: `z-index: 10`, not `10px`.
  bool unitless_property(const std::string &property)
  {
    static const std::vector<std::string> properties = {"aspect-ratio",
                                                        "column-count",
                                                        "flex",
                                                        "flex-grow",
                                                        "flex-shrink",
                                                        "font-weight",
                                                        "grid-column",
                                                        "grid-row",
                                                        "line-height",
                                                        "opacity",
                                                        "order",
                                                        "orphans",
                                                        "tab-size",
                                                        "widows",
                                                        "z-index",
                                                        "zoom"};
    return std::find(properties.begin(), properties.end(), property) != properties.end();
  }

  bool all_digits_or_dot(const std::string &value)
  {
    bool digit = false;
    bool dot = false;
    for (char c : value)
    {
      if (std::isdigit((unsigned char)c))
      {
        digit = true;
        continue;
      }
      if (c == '.' && !dot)
      {
        dot = true;
        continue;
      }
      return false;
    }
    return digit;
  }

  bool all_letters(const std::string &value)
  {
    return !value.empty()
           && std::all_of(value.begin(),
                          value.end(),
                          [](char c) { return std::isalpha((unsigned char)c) != 0; });
  }

  // The unit a trailing letter asks for, or "" when it is not a unit.
  std::string unit_for(char c)
  {
    switch (c)
    {
    case 'p':
      return "%";
    case 'e':
      return "em";
    case 'r':
      return "rem";
    case 'x':
      return "ex";
    default:
      return "";
    }
  }

  // Characters a value may be made of. Anything else means the "abbreviation" is
  // ordinary code and the whole expansion is refused.
  bool value_char(char c)
  {
    switch (c)
    {
    case '#':
    case '%':
    case '.':
    case ',':
    case '/':
    case '*':
    case '(':
    case ')':
    case '"':
    case '\'':
      return true;
    default:
      return std::isalnum((unsigned char)c) != 0 || c == '-' || c == '+';
    }
  }

  // One value of a declaration: `10` -> 10px (or 10), `20p` -> 20%, `#fff` stays
  // a colour, and letters resolve through the property's keyword table and fall
  // back to being the keyword itself, so `ta:middle` still reads.
  std::string format_value(const std::string &property, std::string raw)
  {
    if (raw.empty())
      return raw;
    if (raw[0] == '#')
      return raw;

    if (const std::map<std::string, std::string> *keywords = css_keywords(property))
    {
      const auto found = keywords->find(raw);
      if (found != keywords->end())
        return found->second;
    }
    if (all_letters(raw))
      return raw;

    std::string sign;
    if (raw[0] == '-' || raw[0] == '+')
    {
      sign = raw.substr(0, 1);
      raw.erase(0, 1);
    }
    if (raw.empty())
      return sign;

    // A unit letter sits after the number -- and only there: `inline-table` ends
    // in an `e` that is not an `em`.
    std::string unit;
    if (!std::isdigit((unsigned char)raw.back()))
    {
      const std::string candidate = unit_for(raw.back());
      if (candidate.empty() || !all_digits_or_dot(raw.substr(0, raw.size() - 1)))
        return sign + raw;
      unit = candidate;
      raw.pop_back();
    }
    if (!all_digits_or_dot(raw))
      return sign + raw + unit;
    if (raw == "0")
      return "0";
    if (unit.empty() && !unitless_property(property))
      unit = "px";
    return sign + raw + unit;
  }

  struct Declaration
  {
    std::string property;
    std::vector<std::string> values;
    bool important = false;
  };

  bool parse_declaration(const std::string &text, Declaration &out)
  {
    size_t pos = 0;
    while (pos < text.size() && std::isalpha((unsigned char)text[pos]))
      pos++;
    if (pos == 0)
      return false;
    const std::string head = string_util::lower_copy(text.substr(0, pos));

    // The longest alias (or full property name) the head starts with.
    const char *property = nullptr;
    size_t consumed = 0;
    for (const Alias &alias : css_aliases())
    {
      const size_t len = std::string(alias.short_form).size();
      if (len <= consumed || len > head.size())
        continue;
      if (head.compare(0, len, alias.short_form) != 0)
        continue;
      property = alias.property;
      consumed = len;
    }
    if (property == nullptr)
      return false;
    out.property = property;

    std::string rest = text.substr(consumed);
    if (!rest.empty() && rest[0] == ':')
      rest.erase(0, 1);
    if (!rest.empty() && rest.back() == '!')
    {
      out.important = true;
      rest.pop_back();
    }
    if (rest.empty())
      return true;
    for (char c : rest)
    {
      if (!value_char(c))
        return false;
    }

    // `-` separates two numbers (`m10-20`); anywhere else it is part of the value
    // itself, or a sign. That is what keeps a keyword like `inline-table` a
    // single value and `m-10` minus ten.
    std::string part;
    auto flush = [&]()
    {
      if (!part.empty())
        out.values.push_back(format_value(out.property, part));
      part.clear();
    };
    for (size_t i = 0; i < rest.size(); i++)
    {
      const char c = rest[i];
      if (c == '-' && i > 0 && i + 1 < rest.size() && std::isdigit((unsigned char)rest[i + 1]))
      {
        flush();
        continue;
      }
      part += c;
    }
    flush();
    return !out.values.empty();
  }
} // namespace

namespace Emmet
{
  bool is_markup_file(const std::string &path)
  {
    const std::string lower = string_util::lower_copy(path);
    for (const char *ext : {".html", ".htm", ".jsx", ".tsx"})
    {
      if (string_util::ends_with(lower, ext))
        return true;
    }
    return false;
  }

  bool is_css_file(const std::string &path)
  {
    const std::string lower = string_util::lower_copy(path);
    for (const char *ext : {".css", ".scss", ".sass", ".less"})
    {
      if (string_util::ends_with(lower, ext))
        return true;
    }
    return false;
  }

  bool is_supported_file(const std::string &path)
  {
    return is_markup_file(path) || is_css_file(path);
  }

  std::string abbreviation_before(const std::string &line, int cursor, bool css)
  {
    if (cursor <= 0 || cursor > (int)line.size())
      return "";
    int start = cursor;
    while (start > 0 && !is_space(line[(size_t)start - 1])
           && (css ? css_token_char(line[(size_t)start - 1])
                   : markup_token_char(line[(size_t)start - 1])))
      start--;
    // An operator cannot begin an abbreviation, so a token the scan over-reached
    // into (`>div` after `<section>`, `/div` after `<`) starts at the word.
    while (start < cursor && std::strchr(">+^*/", line[(size_t)start]) != nullptr)
      start++;
    if (start == cursor)
      return "";
    if (!boundary_before(line, start))
      return "";
    const std::string token = line.substr((size_t)start, (size_t)(cursor - start));
    if (token.empty() || std::isdigit((unsigned char)token[0]))
      return "";
    return token;
  }

  bool
  expand_markup(const std::string &abbr, const std::string &indent_unit, bool jsx, Expansion &out)
  {
    if (abbr.empty())
      return false;
    if (abbr == "!")
    {
      out.snippet = "<!DOCTYPE html>$0";
      return true;
    }

    MarkupParser parser(abbr);
    std::vector<Node> roots;
    if (!parser.run(roots))
      return false;

    Node root;
    root.children = std::move(roots);
    expand_repeats(root, 0);

    MarkupOptions opts;
    opts.indent = indent_unit.empty() ? "  " : indent_unit;
    opts.jsx = jsx;
    const Node *caret = caret_leaf(root.children);

    std::string body;
    for (size_t i = 0; i < root.children.size(); i++)
    {
      if (i)
        body += "\n";
      render_node(root.children[i], 0, 0, opts, caret, body);
    }
    if (caret == nullptr)
      body += "$0";

    out.snippet = body;
    return true;
  }

  bool expand_css(const std::string &abbr, const std::string &indent, Expansion &out)
  {
    if (abbr.empty())
      return false;

    std::vector<std::string> parts;
    std::string part;
    for (char c : abbr)
    {
      if (c == '+')
      {
        parts.push_back(part);
        part.clear();
        continue;
      }
      part += c;
    }
    parts.push_back(part);

    std::vector<Declaration> declarations;
    for (const std::string &text : parts)
    {
      if (text.empty())
        return false;
      Declaration declaration;
      if (!parse_declaration(text, declaration))
        return false;
      declarations.push_back(std::move(declaration));
    }

    // The caret goes in the first value the author left open; through a chain of
    // finished declarations it goes at the end of the last line.
    int open_value = -1;
    for (size_t i = 0; i < declarations.size(); i++)
    {
      if (declarations[i].values.empty())
      {
        open_value = (int)i;
        break;
      }
    }

    std::string body;
    for (size_t i = 0; i < declarations.size(); i++)
    {
      const Declaration &declaration = declarations[i];
      if (i)
        body += "\n" + indent;
      body += declaration.property + ":";
      if ((int)i == open_value)
      {
        body += " $0;";
        continue;
      }
      for (const std::string &value : declaration.values)
        body += " " + value;
      if (declaration.important)
        body += " !important";
      body += ";";
    }
    if (open_value < 0)
      body += "$0";

    out.snippet = body;
    return true;
  }
} // namespace Emmet
