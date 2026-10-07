#ifndef FILE_UTIL_H
#define FILE_UTIL_H

#include <string>

namespace file_util
{
  // Writes `content` to `path` through a temporary beside it and a rename.
  //
  // A save that truncates the file in place has a window where a failure -- a
  // full disk, a killed process -- leaves the file shorter than what was on disk
  // a moment ago. Writing the bytes elsewhere and swapping the name closes it:
  // the file either is the old one or the new one, never half of each.
  //
  // An existing file keeps its permission bits, because the temporary a fresh
  // file starts as is not allowed to turn a shared 0644 into a private file.
  //
  // Returns an empty string on success, or the reason it failed.
  std::string write_file_atomic(const std::string &path, const std::string &content);
} // namespace file_util

#endif
