// Internal helpers shared by the workspace modules: file-tree flattening,
// the session-file field encoding used by save/restore and the sidebar tab
// rows, and the empty-scratch-buffer probe used by session restore.
#pragma once

#include "jot/editor_models.h"
#include <string>
#include <vector>

namespace workspace_internal
{
inline void flatten_nodes_mut(std::vector<FileNode> &nodes, std::vector<FileNode *> &flat)
{
  for (auto &node : nodes)
  {
    flat.push_back(&node);
    if (node.is_dir && node.expanded)
    {
      flatten_nodes_mut(node.children, flat);
    }
  }
}

inline void flatten_nodes_const(const std::vector<FileNode> &nodes, std::vector<const FileNode *> &flat)
{
  for (const auto &node : nodes)
  {
    flat.push_back(&node);
    if (node.is_dir && node.expanded)
    {
      flatten_nodes_const(node.children, flat);
    }
  }
}

inline std::string escape_field(const std::string &input)
{
  std::string out;
  out.reserve(input.size());
  for (char c : input)
  {
    if (c == '\\')
    {
      out += "\\\\";
    }
    else if (c == '\t')
    {
      out += "\\t";
    }
    else if (c == '\n')
    {
      out += "\\n";
    }
    else
    {
      out.push_back(c);
    }
  }
  return out;
}

inline std::string unescape_field(const std::string &input)
{
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); i++)
  {
    if (input[i] == '\\' && i + 1 < input.size())
    {
      char n = input[i + 1];
      if (n == 't')
      {
        out.push_back('\t');
        i++;
        continue;
      }
      if (n == 'n')
      {
        out.push_back('\n');
        i++;
        continue;
      }
      if (n == '\\')
      {
        out.push_back('\\');
        i++;
        continue;
      }
    }
    out.push_back(input[i]);
  }
  return out;
}

inline std::vector<std::string> split_tab(const std::string &line)
{
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= line.size())
  {
    size_t pos = line.find('\t', start);
    if (pos == std::string::npos)
    {
      parts.push_back(line.substr(start));
      break;
    }
    parts.push_back(line.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
}

inline bool is_empty_scratch_buffer(const FileBuffer &buf)
{
  return buf.filepath.empty() && !buf.modified && buf.line_count() == 1 && buf.line(0).empty();
}
} // namespace workspace_internal
