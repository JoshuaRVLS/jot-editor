// Lua host bindings: Syntax highlighting bindings (token kind names + highlight query).

#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"

namespace lua_bind
{

  const char *syntax_token_kind_name(int token)
  {
    switch (token)
    {
    case TS_TOKEN_KEYWORD:
      return "keyword";
    case TS_TOKEN_STRING:
      return "string";
    case TS_TOKEN_COMMENT:
      return "comment";
    case TS_TOKEN_NUMBER:
      return "number";
    case TS_TOKEN_TYPE:
      return "type";
    case TS_TOKEN_FUNCTION:
      return "function";
    case TS_TOKEN_VARIABLE:
      return "variable";
    case TS_TOKEN_CONSTANT:
      return "constant";
    case TS_TOKEN_BUILTIN:
      return "builtin";
    case TS_TOKEN_OPERATOR:
      return "operator";
    case TS_TOKEN_PUNCTUATION:
      return "punctuation";
    case TS_TOKEN_TAG:
      return "tag";
    case TS_TOKEN_ATTRIBUTE:
      return "attribute";
    case TS_TOKEN_NAMESPACE:
      return "namespace";
    case TS_TOKEN_MODULE:
      return "module";
    case TS_TOKEN_PARAMETER:
      return "parameter";
    case TS_TOKEN_FIELD:
      return "field";
    case TS_TOKEN_KEYWORD_CONTROL:
      return "keyword_control";
    case TS_TOKEN_KEYWORD_STORAGE:
      return "keyword_storage";
    case TS_TOKEN_KEYWORD_PREPROC:
      return "keyword_preproc";
    case TS_TOKEN_FUNCTION_METHOD:
      return "function_method";
    case TS_TOKEN_FUNCTION_CONSTRUCTOR:
      return "function_constructor";
    case TS_TOKEN_TYPE_BUILTIN:
      return "type_builtin";
    case TS_TOKEN_CONSTANT_MACRO:
      return "constant_macro";
    case TS_TOKEN_STRING_ESCAPE:
      return "string_escape";
    case TS_TOKEN_PUNCTUATION_BRACKET:
      return "punctuation_bracket";
    case TS_TOKEN_PUNCTUATION_DELIMITER:
      return "punctuation_delimiter";
    default:
      return "";
    }
  }
  int l_syntax_highlight(lua_State *L)
  {
    const std::string ext = luaL_optstring(L, 1, "");
    const std::string text = luaL_optstring(L, 2, "");
    SyntaxHighlighter highlighter;
    highlighter.set_language(ext);
    const auto colors = highlighter.get_colors(text);
    lua_newtable(L);
    if (!highlighter.has_rules())
    {
      return 1;
    }
    int out = 1;
    int chunk_start = 0;
    int chunk_token = 0;
    for (int i = 0; i <= (int)colors.size(); i++)
    {
      int token = 0;
      if (i < (int)colors.size() && colors[i].first == 1)
      {
        token = colors[i].second;
      }
      if (i == 0)
      {
        chunk_token = token;
      }
      if (i == (int)colors.size() || token != chunk_token)
      {
        if (i > chunk_start && chunk_token != TS_TOKEN_NONE)
        {
          lua_newtable(L);
          lua_pushinteger(L, chunk_start);
          lua_setfield(L, -2, "start");
          lua_pushinteger(L, i - chunk_start);
          lua_setfield(L, -2, "len");
          lua_pushstring(L, syntax_token_kind_name(chunk_token));
          lua_setfield(L, -2, "kind");
          lua_rawseti(L, -2, out++);
        }
        chunk_start = i;
        chunk_token = token;
      }
    }
    return 1;
  }
} // namespace lua_bind
