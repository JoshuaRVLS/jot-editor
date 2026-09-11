-- Table of contents: heading slugs and the nested list the sidebar renders.
local M = {}

-- GitHub-style anchor slug for a heading, with duplicates disambiguated by a
-- numeric suffix (foo, foo-1, foo-2).
function M.slug(text, state)
  local slug = tostring(text or ""):lower()
  slug = slug:gsub("<[^>]*>", "")
  slug = slug:gsub("[^%w%s%-_]", "")
  slug = slug:gsub("%s+", "-")
  slug = slug:gsub("^%-+", ""):gsub("%-+$", "")
  if slug == "" then
    slug = "section"
  end
  if state then
    local seen = state.seen
    if not seen then
      seen = {}
      state.seen = seen
    end
    if seen[slug] then
      seen[slug] = seen[slug] + 1
      slug = slug .. "-" .. seen[slug]
    else
      seen[slug] = 1
    end
  end
  return slug
end

-- Renders collected headings (`{ level, id, text, html }`) as a nested list.
-- `<li>` elements stay open while a deeper level nests inside them, which is
-- what the browser expects from a multi-level outline.
function M.render(headings)
  if not headings or #headings == 0 then
    return ""
  end
  local out = {}
  local levels = {}
  local function item(h)
    out[#out + 1] = string.format('<li><a href="#%s">%s</a>', h.id, h.html or h.text)
  end
  local function push(level)
    out[#out + 1] = "<ul>"
    levels[#levels + 1] = level
  end
  local function pop()
    out[#out + 1] = "</li></ul>"
    levels[#levels] = nil
  end

  for _, h in ipairs(headings) do
    local level = math.max(1, math.min(6, h.level))
    if #levels == 0 then
      push(level)
      item(h)
    elseif level > levels[#levels] then
      push(level)
      item(h)
    elseif level == levels[#levels] then
      out[#out + 1] = "</li>"
      item(h)
    else
      while #levels > 0 and levels[#levels] > level do
        pop()
      end
      if #levels == 0 then
        push(level)
      else
        out[#out + 1] = "</li>"
      end
      item(h)
    end
  end
  while #levels > 0 do
    pop()
  end
  return table.concat(out)
end

return M
