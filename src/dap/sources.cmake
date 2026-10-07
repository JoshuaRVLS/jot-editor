# The dap module: the Debug Adapter Protocol client and its wire format. It
# speaks the protocol and owns no editor state; the editor wires it up.
set(JOT_DAP_SOURCES
  dap/client.cpp
  dap/convert.cpp
  dap/json.cpp
  dap/process.cpp
)
