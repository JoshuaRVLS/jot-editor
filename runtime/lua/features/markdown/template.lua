-- The preview page document: stylesheet, CDN head tags, table-of-contents
-- sidebar, toolbar, initial body and the live client script.
local assets = require("jot_md.assets")
local inline = require("jot_md.inline")

local M = {}

local AUTO_THEME_JS = [[
  (function () {
    var body = document.body;
    if (body.classList.contains('theme-auto')) {
      body.classList.remove('theme-auto');
      var dark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
      body.classList.add(dark ? 'theme-dark' : 'theme-light');
    }
  })();
]]

-- opts:
--   title        page <title>
--   theme        "dark" | "light" | "auto"
--   body_html    rendered markdown
--   toc_html     rendered table of contents ("" hides the sidebar)
--   options      resolved option table
--   custom_css   extra CSS
function M.document(opts)
  local options = opts.options or {}
  local theme = opts.theme or "dark"
  local theme_class = theme == "light" and "theme-light"
      or theme == "auto" and "theme-auto"
      or "theme-dark"

  local toc_class = (options.toc and opts.toc_html and opts.toc_html ~= "") and "visible" or ""

  local parts = {
    "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n",
    "<meta charset=\"utf-8\">\n",
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n",
    "<link rel=\"icon\" href=\"data:,\">\n",
    "<title>", inline.escape(opts.title or "Markdown Preview"), "</title>\n",
     assets.head_tags(options, theme),
    "\n<style>\n", assets.CSS, "\n", opts.custom_css or "", "\n</style>\n",
    "</head>\n<body class=\"", theme_class, "\">\n",
    "<aside id=\"toc\" class=\"", toc_class, "\">", opts.toc_html or "", "</aside>\n",
    "<div id=\"main\"><div id=\"content\">", opts.body_html or "", "</div></div>\n",
    "<div id=\"toolbar\">\n",
    "<button data-action=\"theme\" type=\"button\" title=\"Toggle theme\">",
    theme == "light" and "dark" or "light", "</button>\n",
    "<button data-action=\"sync\" type=\"button\" title=\"Toggle scroll sync\">sync</button>\n",
    "</div>\n",
    "<div id=\"status\">connecting…</div>\n",
    "<script>\n", AUTO_THEME_JS, "\n</script>\n",
    "<script>\n", assets.CLIENT_JS, "\n</script>\n",
    "</body>\n</html>\n",
  }
  return table.concat(parts)
end

return M
