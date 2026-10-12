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
  // Both walks are iterative on purpose. The tree comes from the filesystem,
  // so its depth is not something this file controls, and a walk that recursed
  // once per level turned a deep enough chain -- an expanded `_deps` tree, a
  // symlinked directory that leads back into the workspace -- into a stack
  // overflow, taken while the sidebar was handling a key. Nodes are pushed in
  // reverse so the flat order is still the visual one: a node, then the rows
  // under it, then its next sibling.
  inline void flatten_nodes_mut(std::vector<FileNode> &nodes, std::vector<FileNode *> &flat)
  {
    std::vector<FileNode *> pending;
    pending.reserve(nodes.size());
    for (auto node = nodes.rbegin(); node != nodes.rend(); ++node)
    {
      pending.push_back(&*node);
    }
    while (!pending.empty())
    {
      FileNode *node = pending.back();
      pending.pop_back();
      flat.push_back(node);
      if (node->is_dir && node->expanded)
      {
        for (auto child = node->children.rbegin(); child != node->children.rend(); ++child)
        {
          pending.push_back(&*child);
        }
      }
    }
  }

  inline void flatten_nodes_const(const std::vector<FileNode> &nodes,
                                  std::vector<const FileNode *> &flat)
  {
    std::vector<const FileNode *> pending;
    pending.reserve(nodes.size());
    for (auto node = nodes.rbegin(); node != nodes.rend(); ++node)
    {
      pending.push_back(&*node);
    }
    while (!pending.empty())
    {
      const FileNode *node = pending.back();
      pending.pop_back();
      flat.push_back(node);
      if (node->is_dir && node->expanded)
      {
        for (auto child = node->children.rbegin(); child != node->children.rend(); ++child)
        {
          pending.push_back(&*child);
        }
      }
    }
  }

  inline bool is_empty_scratch_buffer(const FileBuffer &buf)
  {
    return buf.filepath.empty() && !buf.modified && buf.line_count() == 1 && buf.line(0).empty();
  }
} // namespace workspace_internal
