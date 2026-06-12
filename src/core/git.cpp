#include "editor.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <sys/wait.h>

namespace {
namespace fs = std::filesystem;

std::string trim_right_newlines(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
    s.pop_back();
  }
  return s;
}

std::string shell_quote(const std::string &value) {
  std::string out = "'";
  out.reserve(value.size() + 8);
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('\'');
  return out;
}

std::string capture_command_output(const std::string &command) {
  std::array<char, 512> buf{};
  std::string out;
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) {
    return "";
  }
  while (fgets(buf.data(), (int)buf.size(), pipe) != nullptr) {
    out += buf.data();
  }
  pclose(pipe);
  return out;
}

struct GitCommandResult {
  std::string output;
  int exit_code = 1;

  bool ok() const { return exit_code == 0; }
};

GitCommandResult capture_command_status(const std::string &command) {
  std::array<char, 512> buf{};
  GitCommandResult result;
  FILE *pipe = popen((command + " 2>&1").c_str(), "r");
  if (!pipe) {
    return result;
  }
  while (fgets(buf.data(), (int)buf.size(), pipe) != nullptr) {
    result.output += buf.data();
  }
  int status = pclose(pipe);
  if (status != -1 && WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  }
  result.output = trim_right_newlines(result.output);
  return result;
}

std::string normalize_path(const std::string &path) {
  if (path.empty()) {
    return "";
  }
  std::error_code ec;
  fs::path p = fs::absolute(path, ec);
  if (ec) {
    p = fs::path(path);
  }
  return p.lexically_normal().string();
}

bool starts_with_prefix(const std::string &value, const std::string &prefix) {
  return value.size() >= prefix.size() &&
         value.compare(0, prefix.size(), prefix) == 0;
}

std::string parse_branch_name(const std::string &line) {
  if (line.rfind("## ", 0) != 0) {
    return "";
  }
  std::string body = line.substr(3);
  size_t dots = body.find("...");
  size_t space = body.find(' ');
  size_t cut = std::string::npos;
  if (dots != std::string::npos) {
    cut = dots;
  } else if (space != std::string::npos) {
    cut = space;
  }
  if (cut != std::string::npos) {
    body = body.substr(0, cut);
  }
  if (body == "HEAD" || body.empty()) {
    return "";
  }
  return body;
}

struct GitStatusResult {
  std::string root;
  std::string branch;
  int dirty_count = 0;
  int staged_count = 0;
  int unstaged_count = 0;
  int untracked_count = 0;
  int deleted_count = 0;
  int renamed_count = 0;
  int conflict_count = 0;
  std::unordered_map<std::string, std::string> file_status;
  bool success = false;
};

bool is_conflict_status(const std::string &xy) {
  return xy == "DD" || xy == "AU" || xy == "UD" || xy == "UA" ||
         xy == "DU" || xy == "AA" || xy == "UU" ||
         xy.find('U') != std::string::npos;
}

GitStatusResult run_git_commands(const std::string &repo_hint) {
  GitStatusResult result;

  const std::string top = trim_right_newlines(capture_command_output(
      "git -C " + shell_quote(repo_hint) +
      " rev-parse --show-toplevel 2>/dev/null"));
  if (top.empty()) {
    return result;
  }

  result.root = normalize_path(top);
  result.success = true;

  result.branch = trim_right_newlines(capture_command_output(
      "git -C " + shell_quote(result.root) +
      " symbolic-ref --short HEAD 2>/dev/null"));
  if (result.branch.empty()) {
    result.branch = trim_right_newlines(capture_command_output(
        "git -C " + shell_quote(result.root) +
        " rev-parse --short HEAD 2>/dev/null"));
  }
  if (result.branch.empty()) {
    result.branch = "(detached)";
  }

  const std::string status_text =
      capture_command_output("git -C " + shell_quote(result.root) +
                             " status --porcelain=v1 --branch 2>/dev/null");
  std::istringstream iss(status_text);
  std::string line;
  while (std::getline(iss, line)) {
    if (line.empty()) continue;
    if (line.rfind("## ", 0) == 0) {
      std::string from_status = parse_branch_name(line);
      if (!from_status.empty()) result.branch = from_status;
      continue;
    }
    if (line.size() < 3) continue;
    const std::string xy = line.substr(0, 2);
    std::string rel_path = line.substr(3);
    size_t arrow = rel_path.find(" -> ");
    if (arrow != std::string::npos) {
      rel_path = rel_path.substr(arrow + 4);
    }
    if (!rel_path.empty() && rel_path.front() == '"' &&
        rel_path.back() == '"' && rel_path.size() >= 2) {
      rel_path = rel_path.substr(1, rel_path.size() - 2);
    }
    std::string abs_path =
        normalize_path((fs::path(result.root) / fs::path(rel_path)).string());
    if (!abs_path.empty()) {
      result.file_status[abs_path] = xy;
    }
    if (xy != "  ") {
      result.dirty_count++;
    }
    const bool conflict = is_conflict_status(xy);
    const char index_status = xy[0];
    const char worktree_status = xy[1];
    if (xy == "??") {
      result.untracked_count++;
    } else {
      if (conflict) {
        result.conflict_count++;
      }
      if (!conflict && index_status != ' ' && index_status != '?') {
        result.staged_count++;
      }
      if (!conflict && worktree_status != ' ' && worktree_status != '?') {
        result.unstaged_count++;
      }
      if (index_status == 'D' || worktree_status == 'D') {
        result.deleted_count++;
      }
      if (index_status == 'R' || worktree_status == 'R') {
        result.renamed_count++;
      }
    }
  }

  return result;
}

} // namespace

void Editor::clear_git_status() {
  git_root.clear();
  git_branch.clear();
  git_dirty_count = 0;
  git_staged_count = 0;
  git_unstaged_count = 0;
  git_untracked_count = 0;
  git_deleted_count = 0;
  git_renamed_count = 0;
  git_conflict_count = 0;
  git_file_status.clear();
  invalidate_sidebar_git_cache();
}

bool Editor::has_git_repo() const { return !git_root.empty(); }

std::string Editor::run_git_capture(const std::string &args) const {
  if (git_root.empty()) {
    return "";
  }
  const std::string command = "git -C " + shell_quote(git_root) + " " + args +
                              " 2>/dev/null";
  return trim_right_newlines(capture_command_output(command));
}

std::string Editor::to_git_relative_path(const std::string &path) const {
  if (git_root.empty() || path.empty()) {
    return "";
  }
  std::error_code ec;
  fs::path abs_path = fs::absolute(path, ec);
  if (ec) {
    return path;
  }
  fs::path root = fs::path(git_root);
  fs::path rel = abs_path.lexically_relative(root);
  std::string rel_s = rel.generic_string();
  if (rel_s.empty() || rel_s == "." || starts_with_prefix(rel_s, "../")) {
    return abs_path.generic_string();
  }
  return rel_s;
}

bool Editor::git_stage_path(const std::string &path) {
  if (git_root.empty() || path.empty()) {
    return false;
  }
  fs::path input(path);
  std::string rel = input.is_absolute() ? to_git_relative_path(path)
                                        : input.generic_string();
  if (rel.empty() || rel[0] == '/' || starts_with_prefix(rel, "../")) {
    return false;
  }
  GitCommandResult result = capture_command_status(
      "git -C " + shell_quote(git_root) + " add -A -- " + shell_quote(rel));
  if (result.ok()) {
    refresh_git_status(true);
  }
  return result.ok();
}

bool Editor::git_unstage_path(const std::string &path) {
  if (git_root.empty() || path.empty()) {
    return false;
  }
  fs::path input(path);
  std::string rel = input.is_absolute() ? to_git_relative_path(path)
                                        : input.generic_string();
  if (rel.empty() || rel[0] == '/' || starts_with_prefix(rel, "../")) {
    return false;
  }
  GitCommandResult result = capture_command_status(
      "git -C " + shell_quote(git_root) + " restore --staged -- " +
      shell_quote(rel));
  if (result.ok()) {
    refresh_git_status(true);
  }
  return result.ok();
}

bool Editor::git_stage_all() {
  if (git_root.empty()) {
    return false;
  }
  GitCommandResult result =
      capture_command_status("git -C " + shell_quote(git_root) + " add -A");
  if (result.ok()) {
    refresh_git_status(true);
  }
  return result.ok();
}

bool Editor::git_unstage_all() {
  if (git_root.empty()) {
    return false;
  }
  GitCommandResult result = capture_command_status(
      "git -C " + shell_quote(git_root) + " restore --staged .");
  if (result.ok()) {
    refresh_git_status(true);
  }
  return result.ok();
}

bool Editor::git_commit_message(const std::string &message) {
  if (git_root.empty() || message.empty()) {
    return false;
  }
  GitCommandResult result = capture_command_status(
      "git -C " + shell_quote(git_root) + " commit -m " +
      shell_quote(message));
  if (result.ok()) {
    refresh_git_status(true);
  }
  return result.ok();
}

void Editor::refresh_git_status(bool force) {
  using namespace std::chrono;
  const long long now_ms =
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count();

  if (!force && git_last_refresh_ms > 0 &&
      now_ms - git_last_refresh_ms < 1500) {
    return;
  }

  if (git_refresh_pending_)
    return;

  std::string repo_hint;
  if (workspace_session_enabled && !workspace_session_root.empty()) {
    repo_hint = workspace_session_root;
  } else if (!root_dir.empty()) {
    repo_hint = root_dir;
  } else if (!buffers.empty() && !get_buffer().filepath.empty()) {
    repo_hint = fs::path(get_buffer().filepath).parent_path().string();
  }

  if (repo_hint.empty()) {
    git_last_refresh_ms = now_ms;
    if (!git_root.empty() || !git_file_status.empty() ||
        git_dirty_count != 0 || git_staged_count != 0 ||
        git_unstaged_count != 0 || git_untracked_count != 0 ||
        git_deleted_count != 0 || git_renamed_count != 0 ||
        git_conflict_count != 0 || !git_branch.empty()) {
      clear_git_status();
      needs_redraw = true;
    }
    return;
  }

  git_last_refresh_ms = now_ms;
  git_refresh_pending_ = true;

  if (task_queue_) {
    task_queue_->submit_val<GitStatusResult>(
        [repo_hint = std::move(repo_hint)]() -> GitStatusResult {
          return run_git_commands(repo_hint);
        },
        [this](GitStatusResult result) {
          git_refresh_pending_ = false;

          // Editor may have shut down while the git command was
          // running on the worker thread. Drop the result rather
          // than touching freed state.
          if (!running)
            return;

          if (!result.success) {
            if (!git_root.empty() || !git_file_status.empty() ||
                git_dirty_count != 0 || git_staged_count != 0 ||
                git_unstaged_count != 0 || git_untracked_count != 0 ||
                git_deleted_count != 0 || git_renamed_count != 0 ||
                git_conflict_count != 0 || !git_branch.empty()) {
              clear_git_status();
              needs_redraw = true;
            }
            return;
          }

          const bool changed =
              (result.root != git_root) || (result.branch != git_branch) ||
              (result.dirty_count != git_dirty_count) ||
              (result.staged_count != git_staged_count) ||
              (result.unstaged_count != git_unstaged_count) ||
              (result.untracked_count != git_untracked_count) ||
              (result.deleted_count != git_deleted_count) ||
              (result.renamed_count != git_renamed_count) ||
              (result.conflict_count != git_conflict_count) ||
              (result.file_status != git_file_status);
          git_root = std::move(result.root);
          git_branch = std::move(result.branch);
          git_dirty_count = result.dirty_count;
          git_staged_count = result.staged_count;
          git_unstaged_count = result.unstaged_count;
          git_untracked_count = result.untracked_count;
          git_deleted_count = result.deleted_count;
          git_renamed_count = result.renamed_count;
          git_conflict_count = result.conflict_count;
          git_file_status = std::move(result.file_status);
          if (changed) {
            invalidate_sidebar_git_cache();
            needs_redraw = true;
          }
        });
  } else {
    GitStatusResult result = run_git_commands(repo_hint);
    git_refresh_pending_ = false;

    if (!result.success) {
      if (!git_root.empty() || !git_file_status.empty() ||
          git_dirty_count != 0 || git_staged_count != 0 ||
          git_unstaged_count != 0 || git_untracked_count != 0 ||
          git_deleted_count != 0 || git_renamed_count != 0 ||
          git_conflict_count != 0 || !git_branch.empty()) {
        clear_git_status();
        needs_redraw = true;
      }
      return;
    }

    const bool changed =
        (result.root != git_root) || (result.branch != git_branch) ||
        (result.dirty_count != git_dirty_count) ||
        (result.staged_count != git_staged_count) ||
        (result.unstaged_count != git_unstaged_count) ||
        (result.untracked_count != git_untracked_count) ||
        (result.deleted_count != git_deleted_count) ||
        (result.renamed_count != git_renamed_count) ||
        (result.conflict_count != git_conflict_count) ||
        (result.file_status != git_file_status);
    git_root = std::move(result.root);
    git_branch = std::move(result.branch);
    git_dirty_count = result.dirty_count;
    git_staged_count = result.staged_count;
    git_unstaged_count = result.unstaged_count;
    git_untracked_count = result.untracked_count;
    git_deleted_count = result.deleted_count;
    git_renamed_count = result.renamed_count;
    git_conflict_count = result.conflict_count;
    git_file_status = std::move(result.file_status);
    if (changed) {
      invalidate_sidebar_git_cache();
      needs_redraw = true;
    }
  }
}
