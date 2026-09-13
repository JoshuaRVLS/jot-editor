// Quad batching: all painting appends 6-vertex quads (8 floats each: pos,
// uv, rgba) into one scratch vertex buffer, flushed with a single draw call
// per texture. The buffer is sized for the max grid seen (kMaxBatchVertices
// floats); at realistic grid sizes it can never fill up.
#include "gui/gui.h"

void UIGui::begin_batch()
{
  vertex_.clear();
  vertex_quads_ = 0;
}

void UIGui::push_quad(float x0, float y0, float x1, float y1, float u0, float v0,
                      float u1, float v1, float r, float g, float b, float a)
{
  if (vertex_.size() + 48 > vertex_.capacity())
  {
    return; // scratch full; drop (never happens at realistic grid sizes)
  }
  // Two triangles, appended as one contiguous block rather than 48 push_back
  // calls: a full-height pane is tens of thousands of quads per frame (a
  // background and a glyph for most cells), and at monitor refresh rates the
  // per-element call overhead alone was measurable. The capacity check above
  // guarantees insert() does not reallocate here.
  const float verts[6][8] = {
      {x0, y0, u0, v0, r, g, b, a}, {x1, y0, u1, v0, r, g, b, a},
      {x0, y1, u0, v1, r, g, b, a}, {x0, y1, u0, v1, r, g, b, a},
      {x1, y0, u1, v0, r, g, b, a}, {x1, y1, u1, v1, r, g, b, a},
  };
  vertex_.insert(vertex_.end(), &verts[0][0], &verts[0][0] + 48);
  vertex_quads_++;
}

void UIGui::end_batch()
{
  if (vertex_.empty())
  {
    return;
  }
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_.size() * sizeof(float), vertex_.data());
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(vertex_.size() / 8));
}

void UIGui::flush_tex(unsigned int tex)
{
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glUniform1i(u_tex_loc_, 0);
}