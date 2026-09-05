#ifndef JOT_DECORATIONS_H
#define JOT_DECORATIONS_H

#include "core/types.h"

#include <cstdint>
#include <string>
#include <vector>

// Shifts every decoration from `base`-space into `current`-space, assuming at
// most one contiguous edit between the two texts (see decorations.cpp for the
// exact gravity semantics). Pure function: unit-testable without an Editor.
void decoration_anchor_transform(std::vector<Decoration> &decorations,
                                 const std::string &base,
                                 const std::string &current);

// Inserts a decoration keeping the vector sorted by (row, col, priority, id)
// and marks the buffer as needing re-anchoring.
void decoration_insert(FileBuffer &buf, Decoration decoration);

// Removes the decoration with `id`; returns true when found.
bool decoration_erase(FileBuffer &buf, std::uint64_t id);

#endif