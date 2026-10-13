-- The reference grammar of the Bible reader: what a book answers to, how a
-- typed reference is read, and how it prints back.
--
-- This is the policy half of src/extras/bible.cpp, moved out of the binary: a
-- reader is the one who would want `1 Cor 13` to work, or to call the book
-- `Joao`, and none of that should cost a rebuild. The C++ side keeps its own
-- copy and uses it whenever this module is not loaded, so a broken edit here
-- costs a fallback, not a broken reader.
--
-- `books` lists the id every fetch URL uses, the name a reference prints as and
-- the spellings it answers to. The chapter count is not here: the limits a
-- reference is checked against come from the fetched translation, and the
-- native canon holds them.
--
-- Matching ignores case, spaces and punctuation (see normalize), so "1 Cor 13"
-- and "first corinthians 13" find the same book.
local M = {}

local books = {
  { id = "GEN", name = "Genesis", aliases = "gen ge gn genesis" },
  { id = "EXO", name = "Exodus", aliases = "exo ex exod exodus" },
  { id = "LEV", name = "Leviticus", aliases = "lev lv leviticus" },
  { id = "NUM", name = "Numbers", aliases = "num nu nm numbers" },
  { id = "DEU", name = "Deuteronomy", aliases = "deu deut dt deuteronomy" },
  { id = "JOS", name = "Joshua", aliases = "jos josh joshua" },
  { id = "JDG", name = "Judges", aliases = "jdg judg jg jsh judges" },
  { id = "RUT", name = "Ruth", aliases = "rut rth ruth" },
  { id = "1SA", name = "1 Samuel", aliases = "1sa 1sam 1samuel 1sm 1s firstsamuel" },
  { id = "2SA", name = "2 Samuel", aliases = "2sa 2sam 2samuel 2sm 2s secondsamuel" },
  { id = "1KI", name = "1 Kings", aliases = "1ki 1kgs 1kg 1kings firstkings" },
  { id = "2KI", name = "2 Kings", aliases = "2ki 2kgs 2kg 2kings secondkings" },
  { id = "1CH", name = "1 Chronicles", aliases = "1ch 1chr 1chron 1chronicles firstchronicles" },
  { id = "2CH", name = "2 Chronicles", aliases = "2ch 2chr 2chron 2chronicles secondchronicles" },
  { id = "EZR", name = "Ezra", aliases = "ezr ezra" },
  { id = "NEH", name = "Nehemiah", aliases = "neh ne nehemiah" },
  { id = "EST", name = "Esther", aliases = "est es esther" },
  { id = "JOB", name = "Job", aliases = "job jb job" },
  { id = "PSA", name = "Psalms", aliases = "psa ps pss psalm psalms" },
  { id = "PRO", name = "Proverbs", aliases = "pro prov prv proverbs" },
  { id = "ECC", name = "Ecclesiastes", aliases = "ecc eccl eccles ecclesiastes qoheleth" },
  { id = "SNG", name = "Song of Solomon", aliases = "sng sos song songs songofsolomon songofsongs canticles" },
  { id = "ISA", name = "Isaiah", aliases = "isa is isaiah" },
  { id = "JER", name = "Jeremiah", aliases = "jer je jeremiah" },
  { id = "LAM", name = "Lamentations", aliases = "lam la lamentations" },
  { id = "EZK", name = "Ezekiel", aliases = "ezk ezek ezekiel" },
  { id = "DAN", name = "Daniel", aliases = "dan dn da daniel" },
  { id = "HOS", name = "Hosea", aliases = "hos ho hosea" },
  { id = "JOL", name = "Joel", aliases = "jol joe joel" },
  { id = "AMO", name = "Amos", aliases = "amo am amos" },
  { id = "OBA", name = "Obadiah", aliases = "oba ob obadiah" },
  { id = "JON", name = "Jonah", aliases = "jon jonah" },
  { id = "MIC", name = "Micah", aliases = "mic mi micah" },
  { id = "NAM", name = "Nahum", aliases = "nam nah na nahum" },
  { id = "HAB", name = "Habakkuk", aliases = "hab habakkuk" },
  { id = "ZEP", name = "Zephaniah", aliases = "zep zeph zephaniah" },
  { id = "HAG", name = "Haggai", aliases = "hag haggai" },
  { id = "ZEC", name = "Zechariah", aliases = "zec zech zechariah" },
  { id = "MAL", name = "Malachi", aliases = "mal ml malachi" },
  { id = "MAT", name = "Matthew", aliases = "mat matt mt matthew" },
  { id = "MRK", name = "Mark", aliases = "mrk mk mr mark" },
  { id = "LUK", name = "Luke", aliases = "luk lk luke" },
  { id = "JHN", name = "John", aliases = "jhn jn john" },
  { id = "ACT", name = "Acts", aliases = "act acts" },
  { id = "ROM", name = "Romans", aliases = "rom ro rm romans" },
  { id = "1CO", name = "1 Corinthians", aliases = "1co 1cor 1corinthians firstcorinthians" },
  { id = "2CO", name = "2 Corinthians", aliases = "2co 2cor 2corinthians secondcorinthians" },
  { id = "GAL", name = "Galatians", aliases = "gal ga galatians" },
  { id = "EPH", name = "Ephesians", aliases = "eph ephesians" },
  { id = "PHP", name = "Philippians", aliases = "php phil philippians" },
  { id = "COL", name = "Colossians", aliases = "col colossians" },
  { id = "1TH", name = "1 Thessalonians", aliases = "1th 1thess 1thessalonians firstthessalonians" },
  { id = "2TH", name = "2 Thessalonians", aliases = "2th 2thess 2thessalonians secondthessalonians" },
  { id = "1TI", name = "1 Timothy", aliases = "1ti 1tim 1timothy firsttimothy" },
  { id = "2TI", name = "2 Timothy", aliases = "2ti 2tim 2timothy secondtimothy" },
  { id = "TIT", name = "Titus", aliases = "tit ti titus" },
  { id = "PHM", name = "Philemon", aliases = "phm phlm philemon" },
  { id = "HEB", name = "Hebrews", aliases = "heb hebrews" },
  { id = "JAS", name = "James", aliases = "jas jm james" },
  { id = "1PE", name = "1 Peter", aliases = "1pe 1pet 1peter firstpeter" },
  { id = "2PE", name = "2 Peter", aliases = "2pe 2pet 2peter secondpeter" },
  { id = "1JN", name = "1 John", aliases = "1jn 1jhn 1john firstjohn" },
  { id = "2JN", name = "2 John", aliases = "2jn 2jhn 2john secondjohn" },
  { id = "3JN", name = "3 John", aliases = "3jn 3jhn 3john thirdjohn" },
  { id = "JUD", name = "Jude", aliases = "jud jude" },
  { id = "REV", name = "Revelation", aliases = "rev re revelation revelations apocalypse" },
}

-- Lowercase, spell out the ordinals, drop everything that is not a letter or a
-- digit: the form both the alias map and a typed reference are compared in.
local function normalize(text)
  local lowered = text:lower()
  lowered = lowered:gsub("1st", "1"):gsub("2nd", "2"):gsub("3rd", "3")
  return (lowered:gsub("[^%w]", ""))
end

-- alias -> book, built once from the two spellings every book has (its id and
-- its name) plus the alias list.
local index = nil
local function alias_index()
  if index then
    return index
  end
  index = {}
  for _, book in ipairs(books) do
    index[normalize(book.id)] = book
    index[normalize(book.name)] = book
    for token in book.aliases:gmatch("%S+") do
      index[normalize(token)] = book
    end
  end
  return index
end

-- The book `text` names, or nil. `text` may carry spaces, dots and the ordinal
-- ("1 cor", "1cor", "1 corinthians").
local function find_book(text)
  return alias_index()[normalize(text)]
end

-- "John 3:16-17", "John 3:16", "John 3", "1 Corinthians 13:4-7".
function M.label(reference)
  if not reference or not reference.id then
    return ""
  end
  local book = find_book(reference.id)
  local name = book and book.name or reference.id
  local out = name .. " " .. tostring(reference.chapter)
  if reference.has_verse then
    out = out .. ":" .. tostring(reference.verse_first)
    if (reference.verse_last or 0) > reference.verse_first then
      out = out .. "-" .. tostring(reference.verse_last)
    end
  end
  return out
end

local function is_digit(c)
  return c ~= nil and c:match("%d") ~= nil
end

-- The full reference parsed, or nil when no book name leads a chapter number.
-- A reference past the book's last chapter or with a nonsense verse (0, or a
-- span that runs backwards) is still parsed: the caller decides what to report,
-- so a typo gets a message instead of silence.
function M.parse(text)
  if type(text) ~= "string" then
    return nil
  end
  -- Collapse runs of whitespace so "1 cor 13" and "1  cor 13" read alike.
  local collapsed = text:lower():gsub("%s+", " ")
  collapsed = collapsed:gsub("^%s+", ""):gsub("%s+$", "")
  collapsed = collapsed:gsub("[%.,]+$", "")
  if collapsed == "" then
    return nil
  end

  local n = #collapsed
  for i = 1, n do
    local book_raw = collapsed:sub(1, i):gsub("[%s.]+$", "")
    if book_raw ~= "" then
      local book = find_book(book_raw)
      if book then
        local p = i
        while p < n and collapsed:sub(p + 1, p + 1) == " " do
          p = p + 1
        end
        if p >= n then
          -- A bare book name means its first chapter.
          if i == n then
            return { id = book.id, chapter = 1, has_verse = false, verse_first = 0, verse_last = 0 }
          end
        elseif is_digit(collapsed:sub(p + 1, p + 1)) then
          local chapter = 0
          while p < n and is_digit(collapsed:sub(p + 1, p + 1)) do
            chapter = chapter * 10 + tonumber(collapsed:sub(p + 1, p + 1))
            p = p + 1
          end
          if chapter > 0 then
            local has_verse = false
            local verse_first = 0
            local verse_last = 0
            local q = p
            while q < n and collapsed:sub(q + 1, q + 1) == " " do
              q = q + 1
            end
            local ok = true
            if q < n then
              local ch = collapsed:sub(q + 1, q + 1)
              if ch == ":" or ch == "." or ch == "," then
                q = q + 1
                while q < n and collapsed:sub(q + 1, q + 1) == " " do
                  q = q + 1
                end
              elseif q == p then
                -- "john3abc": digits ran straight into something that is not a
                -- verse separator, so this split is not a reference.
                ok = false
              end
              if ok and (q >= n or not is_digit(collapsed:sub(q + 1, q + 1))) then
                ok = false
              end
              if ok then
                while q < n and is_digit(collapsed:sub(q + 1, q + 1)) do
                  verse_first = verse_first * 10 + tonumber(collapsed:sub(q + 1, q + 1))
                  q = q + 1
                end
                verse_last = verse_first
                local r = q
                while r < n and collapsed:sub(r + 1, r + 1) == " " do
                  r = r + 1
                end
                local dash = collapsed:sub(r + 1, r + 1)
                local dots = dash == "." and collapsed:sub(r + 2, r + 2) == "."
                if r < n and (dash == "-" or dots) then
                  local r2 = dash == "-" and r + 1 or r + 2
                  while r2 < n and collapsed:sub(r2 + 1, r2 + 1) == " " do
                    r2 = r2 + 1
                  end
                  if r2 < n and is_digit(collapsed:sub(r2 + 1, r2 + 1)) then
                    verse_last = 0
                    while r2 < n and is_digit(collapsed:sub(r2 + 1, r2 + 1)) do
                      verse_last = verse_last * 10 + tonumber(collapsed:sub(r2 + 1, r2 + 1))
                      r2 = r2 + 1
                    end
                    q = r2
                  end
                end
                while q < n and collapsed:sub(q + 1, q + 1) == " " do
                  q = q + 1
                end
                if q ~= n or verse_first <= 0 then
                  ok = false
                else
                  has_verse = true
                end
              end
            elseif q ~= n then
              ok = false
            end
            if ok then
              return {
                id = book.id,
                chapter = chapter,
                has_verse = has_verse,
                verse_first = has_verse and verse_first or 0,
                verse_last = has_verse and math.max(verse_first, verse_last) or 0,
              }
            end
          end
        end
      end
    end
  end
  return nil
end

return M
