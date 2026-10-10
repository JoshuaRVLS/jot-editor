-- Adapters and settings for the AI assistant (features/ai/*, loaded by
-- src/lua/api/api_runtime.cpp).
--
-- Everything provider-specific lives in the table below: how to speak to it
-- (`kind`), where it is (`base`), which environment variable holds the token
-- (`key`) and a starting `model`. `kind` is a wire protocol, not a vendor, so
-- a provider that speaks one of the three needs no code at all - only an
-- entry:
--
--   "openai"    chat-completions: OpenAI, and the many services that copy the
--               shape (Ollama, OpenRouter, DeepSeek, Groq, Mistral, xAI,
--               Gemini's compatibility endpoint, LM Studio, llama.cpp, vLLM, a
--               company gateway)
--   "anthropic" the messages API
--   "cmd"       a CLI already on PATH that answers on stdout
--
-- Every value is overridable from config: ai_adapter picks the entry, and
-- ai_model / ai_base_url / ai_key_env / ai_stream / ai_temperature /
-- ai_max_tokens / ai_system_prompt / ai_command replace the entry's own.
local M = {}

M.adapters = {
  openai = {
    kind = "openai",
    base = "https://api.openai.com/v1",
    key = "OPENAI_API_KEY",
    model = "gpt-4o-mini",
  },
  anthropic = {
    kind = "anthropic",
    base = "https://api.anthropic.com/v1",
    key = "ANTHROPIC_API_KEY",
    model = "claude-3-5-sonnet-latest",
    -- The messages API refuses a request without it.
    max_tokens = 4096,
  },
  ollama = { kind = "openai", base = "http://localhost:11434/v1", model = "llama3.2" },
  lmstudio = { kind = "openai", base = "http://localhost:1234/v1", model = "local-model" },
  ["llama.cpp"] = { kind = "openai", base = "http://localhost:8080/v1", model = "local-model" },
  openrouter = {
    kind = "openai",
    base = "https://openrouter.ai/api/v1",
    key = "OPENROUTER_API_KEY",
    model = "openai/gpt-4o-mini",
  },
  deepseek = {
    kind = "openai",
    base = "https://api.deepseek.com/v1",
    key = "DEEPSEEK_API_KEY",
    model = "deepseek-chat",
  },
  groq = {
    kind = "openai",
    base = "https://api.groq.com/openai/v1",
    key = "GROQ_API_KEY",
    model = "llama-3.3-70b-versatile",
  },
  mistral = {
    kind = "openai",
    base = "https://api.mistral.ai/v1",
    key = "MISTRAL_API_KEY",
    model = "mistral-large-latest",
  },
  xai = { kind = "openai", base = "https://api.x.ai/v1", key = "XAI_API_KEY", model = "grok-2-latest" },
  gemini = {
    kind = "openai",
    base = "https://generativelanguage.googleapis.com/v1beta/openai",
    key = "GEMINI_API_KEY",
    model = "gemini-2.0-flash",
  },
  together = {
    kind = "openai",
    base = "https://api.together.xyz/v1",
    key = "TOGETHER_API_KEY",
    model = "meta-llama/Llama-3.3-70B-Instruct-Turbo",
  },
  -- A CLI on PATH, the prompt piped to its stdin. Whatever it prints is the
  -- answer; `stream` stays off because a CLI buffers its own output.
  cli = { kind = "cmd", command = "claude -p", model = "cli", stream = false },
}

-- Answer in the shape the chat buffer is built to read: markdown, code in
-- fenced blocks, and an edit given as the code rather than described.
M.DEFAULT_SYSTEM = table.concat({
  "You are the coding assistant inside the jot editor.",
  "Answer in markdown; put code in fenced blocks tagged with its language.",
  "When asked to change code, give the changed code, not a description of it.",
}, " ")

-- Config reads that treat an unset key and an emptied one the same way, so
-- clearing a row in the settings menu falls back to the adapter's own value.
local function cfg_str(key, fallback)
  local value = jot.config.get(key)
  if type(value) == "string" and value ~= "" then
    return value
  end
  return fallback
end

local function cfg_bool(key, fallback)
  local value = jot.config.get_bool(key)
  if value == nil then
    return fallback
  end
  return value and true or false
end

local function cfg_num(key, fallback)
  local value = jot.config.get_number(key)
  if type(value) ~= "number" then
    return fallback
  end
  return value
end

-- The adapter entries, keyed by name, in the order the pickers list them.
function M.names()
  local names = {}
  for name in pairs(M.adapters) do
    names[#names + 1] = name
  end
  table.sort(names)
  return names
end

function M.entry(name)
  return M.adapters[name]
end

-- The resolved adapter: the named entry with every config override applied and
-- the token already read from the environment. `overrides` ({adapter=, model=})
-- win over config, which is how a chat session pins its own model.
function M.resolve(overrides)
  overrides = overrides or {}
  local name = overrides.adapter or cfg_str("ai_adapter", "openai")
  local entry = M.adapters[name] or M.adapters.openai
  local key_env = cfg_str("ai_key_env", entry.key or "")
  local key = ""
  if key_env ~= "" then
    key = os.getenv(key_env) or ""
  end
  return {
    name = M.adapters[name] and name or "openai",
    kind = entry.kind,
    base = cfg_str("ai_base_url", entry.base or ""),
    model = overrides.model or cfg_str("ai_model", entry.model or ""),
    key = key,
    key_env = key_env,
    command = cfg_str("ai_command", entry.command or ""),
    stream = cfg_bool("ai_stream", entry.stream ~= false),
    temperature = cfg_num("ai_temperature", entry.temperature or 0.2),
    max_tokens = cfg_num("ai_max_tokens", entry.max_tokens or 0),
    system = cfg_str("ai_system_prompt", M.DEFAULT_SYSTEM),
  }
end

-- One line for the pickers and :CodeCompanionStatus: what is talking to what.
function M.describe(adapter)
  adapter = adapter or M.resolve()
  local where = adapter.kind == "cmd" and adapter.command or adapter.base
  local missing = ""
  if adapter.kind ~= "cmd" and adapter.key_env ~= "" and adapter.key == "" then
    missing = "  (no " .. adapter.key_env .. ")"
  end
  return ("%s  %s  %s%s"):format(adapter.name, adapter.model, where, missing)
end

-- A one-line explanation of why a request cannot start yet.
function M.blocked(adapter)
  if adapter.kind == "cmd" then
    if adapter.command == "" then
      return "no command for the cli adapter (set ai_command)"
    end
    return nil
  end
  if adapter.base == "" then
    return "no endpoint (set ai_base_url)"
  end
  if adapter.model == "" then
    return "no model (set ai_model)"
  end
  if adapter.key_env ~= "" and adapter.key == "" then
    return "missing " .. adapter.key_env
  end
  return nil
end

return M
