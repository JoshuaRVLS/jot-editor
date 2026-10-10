# The dap module: the Debug Adapter Protocol client and its wire format, plus
# the editor-side debugger -- the adapter registry, breakpoints, launch
# configuration and the live session -- and the model its panel draws.
set(JOT_DAP_SOURCES
  dap/client.cpp
  dap/convert.cpp
  dap/debugger_adapters.cpp
  dap/debugger_breakpoints.cpp
  dap/debugger_config.cpp
  dap/debugger_session.cpp
  dap/json.cpp
  dap/process.cpp
)
