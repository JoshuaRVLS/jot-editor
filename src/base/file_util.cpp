#include "base/file_util.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace file_util
{
  std::string write_file_atomic(const std::string &path, const std::string &content)
  {
    const fs::path target(path);
    const fs::path temporary = fs::path(path + ".jot-saving");

    std::error_code ec;
    {
      std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
      if (!out.is_open())
      {
        return "cannot write " + temporary.string();
      }
      out.write(content.data(), static_cast<std::streamsize>(content.size()));
      out.flush();
      if (!out.good())
      {
        out.close();
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return "write error";
      }
    }

    // Carry the mode over before the swap, so the file is never briefly readable
    // by nobody but its owner. A file that does not exist yet keeps whatever the
    // umask gave the temporary, which is what creating it directly would have.
    const fs::file_status existing = fs::status(target, ec);
    if (!ec && fs::exists(existing))
    {
      std::error_code chmod_error;
      fs::permissions(temporary, existing.permissions(), chmod_error);
    }

#ifdef _WIN32
    // There rename() refuses an existing destination, so the old file goes first.
    // That gap is the one thing this cannot make atomic there.
    std::error_code remove_error;
    fs::remove(target, remove_error);
#endif

    fs::rename(temporary, target, ec);
    if (ec)
    {
      std::error_code ignored;
      fs::remove(temporary, ignored);
      return "cannot replace " + path + ": " + ec.message();
    }
    return {};
  }
} // namespace file_util
