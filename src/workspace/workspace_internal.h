// Internal helpers shared by the workspace modules: file-tree flattening,
// the session-file field encoding used by save/restore and the sidebar tab
// rows, and the empty-scratch-buffer probe used by session restore.
#pragma once

#include "base/string_util.h"
#include "core/editor_models.h"
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

  inline void flatten_nodes_const(const std::vector<FileNode> &nodes,
                                  std::vector<const FileNode *> &flat)
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

  inline bool is_empty_scratch_buffer(const FileBuffer &buf)
  {
    return buf.filepath.empty() && !buf.modified && buf.line_count() == 1 && buf.line(0).empty();
  }
} // namespace workspace_internal
