// The statusline's two time labels (see status_clock.h).
#include "extras/status_clock.h"

#include <cstdio>

namespace status_clock
{
  std::string format_clock(std::time_t wall)
  {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &wall);
#else
    localtime_r(&wall, &tm);
#endif
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
  }

  std::string format_duration(long long elapsed_ms)
  {
    const long long seconds = elapsed_ms > 0 ? elapsed_ms / 1000 : 0;
    if (seconds < 60)
    {
      return std::to_string(seconds) + "s";
    }
    const long long minutes = seconds / 60;
    if (minutes < 60)
    {
      return std::to_string(minutes) + "m";
    }
    char buf[24];
    // Minutes are padded so the label keeps its width while the seconds field
    // is not what is moving any more.
    std::snprintf(buf, sizeof(buf), "%lldh %02lldm", minutes / 60, minutes % 60);
    return buf;
  }
} // namespace status_clock
