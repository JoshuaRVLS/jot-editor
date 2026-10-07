# The git module: reading repository state, driving the git client, the panel's
# actions and the two painters that draw the log and the diff.
set(JOT_GIT_SOURCES
  git/git.cpp
  git/git_client.cpp
  git/git_diff.cpp
  git/git_panel.cpp
  git/git_panel_paint.cpp
)
