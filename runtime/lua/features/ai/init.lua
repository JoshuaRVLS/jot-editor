-- The AI assistant, registered as `jot.ai` (features/ai/*.lua, loaded by
-- src/jot/lua/api/api_runtime.cpp).
--
-- A port of CodeCompanion.nvim's shape onto jot: a chat buffer you type into
-- and send from, `/` commands and `#` context references inside a prompt, an
-- inline rewrite previewed as a diff, and adapters for the providers that
-- speak the OpenAI or Anthropic APIs plus any CLI on PATH.
--
--   :CodeCompanionChat       open or close the chat      Alt+Shift+A C
--   :CodeCompanionNewChat    start a fresh chat          Alt+Shift+A N
--   :CodeCompanionSend       send the prompt             Alt+Shift+A S
--   :CodeCompanionStop       stop the answer             Alt+Shift+A X
--   :CodeCompanionActions    prompts, adapters, commands Alt+Shift+A A
--   :CodeCompanionStatus     adapter, model, endpoint
--   :CodeCompanion <text>    rewrite the selection/line  Alt+Shift+A I
--   :CodeCompanionPrompt <n> rewrite with a named prompt Alt+Shift+A P
--
-- The settings live under `ai_` (`:settings`, section "AI assistant"):
-- adapter, model, endpoint, key variable, temperature, max tokens, streaming
-- and the system prompt. The token itself is only ever read from the
-- environment variable named there, never stored in the config.
local chat = require("jot_ai.chat")
local config = require("jot_ai.config")
local inline = require("jot_ai.inline")

local M = {}

-- The prompt library: `:CodeCompanionActions` (and Alt+Shift+A P) lists these
-- and runs the chosen one through the inline assistant. A user or plugin can
-- add more with `jot.ai.setup({ prompts = { ... } })`.
M.prompts = {
  explain = "Explain what this code does and why, in a few sentences.",
  fix = "Find the bug in this code and give the corrected version.",
  tests = "Write tests for this code, in the same language and style.",
  refactor = "Refactor this code for clarity without changing what it does.",
  document = "Add documentation comments to this code.",
  faster = "Make this code faster, and say what changed.",
}

local function prompt_names()
  local names = {}
  for name in pairs(M.prompts) do
    names[#names + 1] = name
  end
  table.sort(names)
  return names
end

local function notify(message)
  jot.ui.show_message("[ai] " .. message)
end

function M.setup(opts)
  for name, text in pairs((opts or {}).prompts or {}) do
    M.prompts[name] = text
  end
  if opts then
    if opts.adapter then
      jot.config.set("ai_adapter", opts.adapter)
    end
    if opts.model then
      jot.config.set("ai_model", opts.model)
    end
  end
end

function M.status()
  jot.ui.popup(chat.status_line(), " AI ")
end

-- Runs a named prompt from the library through the inline assistant.
function M.prompt(name)
  local text = M.prompts[name]
  if not text then
    notify("unknown prompt: " .. tostring(name))
    return false
  end
  return inline.run(text)
end

-- The picker behind :CodeCompanionActions and Alt+Shift+A A: the prompts, the
-- adapters and the chat commands in one list. Labels carry their family as a
-- prefix, which is what the select handler dispatches on.
function M.actions()
  local items = {
    "Chat: open or close",
    "Chat: new",
    "Chat: send the prompt",
    "Chat: stop",
    "Chat: status",
  }
  for _, name in ipairs(config.names()) do
    local entry = config.entry(name) or {}
    items[#items + 1] = ("Adapter: %s · %s"):format(name, entry.model or "")
  end
  for _, name in ipairs(prompt_names()) do
    items[#items + 1] = ("Prompt: %s · %s"):format(name, M.prompts[name])
  end
  jot.ui.picker("CodeCompanion", items, function(label)
    local family, rest = label:match("^(%a+):%s*(.*)$")
    if family == "Chat" then
      if rest == "open or close" then
        chat.toggle()
      elseif rest == "new" then
        chat.reset()
      elseif rest == "send the prompt" then
        chat.send()
      elseif rest == "stop" then
        chat.stop()
      elseif rest == "status" then
        M.status()
      end
      return
    end
    if family == "Adapter" then
      local name = rest:match("^([^·]+)")
      if name then
        name = name:gsub("%s+$", "")
        chat.set_adapter(name)
        notify("adapter: " .. name)
      end
      return
    end
    if family == "Prompt" then
      local name = rest:match("^([^·]+)")
      if name then
        M.prompt(name:gsub("%s+$", ""))
      end
    end
  end)
end

local function key(chord, fn, detail)
  if jot.keymap and jot.keymap.remove then
    -- A reload re-runs this file; without this the same chord would be
    -- registered twice and the first (stale) callback would keep winning.
    jot.keymap.remove(chord)
  end
  jot.keymap.set(chord, fn, detail)
end

key("Alt+Shift+A", "", "AI")
key("Alt+Shift+A C", function() chat.toggle() end, "AI: chat")
key("Alt+Shift+A N", function() chat.reset() end, "AI: new chat")
key("Alt+Shift+A S", function() chat.send() end, "AI: send the prompt")
key("Alt+Shift+A X", function() chat.stop() end, "AI: stop answering")
key("Alt+Shift+A A", function() M.actions() end, "AI: actions")
key("Alt+Shift+A P", function() M.actions() end, "AI: prompts")
key("Alt+Shift+A I", function() inline.run("Improve this code.") end, "AI: rewrite the selection")
key("Alt+Shift+A H", function() M.status() end, "AI: status")

jot.command("CodeCompanionChat", function() chat.toggle() end, "AI: open or close the chat")
jot.command("Cc", function() chat.toggle() end, "AI: open or close the chat")
jot.command("CodeCompanionNewChat", function() chat.reset() end, "AI: start a fresh chat")
jot.command("CodeCompanionSend", function() chat.send() end, "AI: send the prompt")
jot.command("CodeCompanionStop", function() chat.stop() end, "AI: stop the answer")
jot.command("CodeCompanionActions", function() M.actions() end, "AI: prompts, adapters, commands")
jot.command("CodeCompanionStatus", function() M.status() end, "AI: adapter, model, endpoint")
jot.command("CodeCompanionPrompt", function(name) M.prompt((name or ""):gsub("%s+$", "")) end,
            "AI: run a named prompt")
jot.command("CodeCompanion", function(text)
  text = (text or ""):gsub("^%s+", ""):gsub("%s+$", "")
  if text == "" then
    M.actions()
    return
  end
  inline.run(text)
end, "AI: rewrite the selection or line")

jot.ai = {
  setup = M.setup,
  prompts = M.prompts,
  prompt = M.prompt,
  actions = M.actions,
  status = M.status,
  chat = {
    open = chat.open,
    toggle = chat.toggle,
    send = chat.send,
    stop = chat.stop,
    reset = chat.reset,
    set_adapter = chat.set_adapter,
    messages = chat.messages,
  },
  inline = inline.run,
  adapters = config.names,
  active = config.resolve,
  config = config,
}

return M
