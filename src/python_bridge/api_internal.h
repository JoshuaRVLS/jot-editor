#ifndef PYTHON_BRIDGE_API_INTERNAL_H
#define PYTHON_BRIDGE_API_INTERNAL_H

class PythonAPI;

namespace PythonBridgeInternal {
bool register_internal_module();
void set_active_api(PythonAPI *api);
} // namespace PythonBridgeInternal

#endif
