// Internal helpers shared across the debugger integration modules: adapter
// resolution (debugger_adapters.cpp), config paths (debugger_config.cpp),
// breakpoint path normalization (debugger_breakpoints.cpp), and output
// compaction (debugger_session.cpp).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace debugger_internal
{
// debugger_adapters.cpp
bool command_exists(const std::string &name);
std::vector<std::string> adapter_command_for(const std::string &adapter);
std::string adapter_binary_for(const std::string &adapter);
std::vector<std::string> split_shell_words(const std::string &text);

// debugger_config.cpp
std::string default_debug_config_path(const std::string &root_dir);

// debugger_breakpoints.cpp
std::string normalize_path_string(const std::string &path);

// debugger_session.cpp
std::string compact_output(std::string text, size_t max_size = 64000);
} // namespace debugger_internal