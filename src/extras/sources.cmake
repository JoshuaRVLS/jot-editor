# The extras module: the optional features the core is not allowed to include.
# The bible verse, coding time, discord presence, emmet expansion, the LeetCode
# bridge and the WakaTime heartbeat all hook into the editor from the outside.
set(JOT_EXTRAS_SOURCES
  extras/bible.cpp
  extras/bible_client.cpp
  extras/coding_time/probe.cpp
  extras/coding_time/wiring.cpp
  extras/discord_session.cpp
  extras/emmet/expand.cpp
  extras/emmet/wiring.cpp
  extras/leetcode_http.cpp
  extras/status_clock.cpp
  extras/wakatime.cpp
)
