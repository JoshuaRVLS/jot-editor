#include "command_utils.h"
#include "editor.h"

#include <algorithm>
#include <sstream>

using namespace CommandLineUtils;

void Editor::handle_command_palette(int ch) {
  auto reset_completion_state = [&]() {
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    command_palette_selected = 0;
  };

  auto apply_selected_completion = [&]() {
    if (command_palette_results.empty()) {
      return;
    }

    const std::string seed = command_palette_theme_mode
                                 ? command_palette_theme_original
                                 : command_palette_query;
    const bool has_colon = !seed.empty() && seed[0] == ':';
    std::string body = has_colon ? seed.substr(1) : seed;
    size_t body_start = body.find_first_not_of(" \t");
    std::string parse_body =
        body_start == std::string::npos ? "" : body.substr(body_start);

    std::string cmd;
    std::string arg;
    std::string raw_arg;
    std::istringstream iss(parse_body);
    iss >> cmd;
    std::getline(iss, raw_arg);
    arg = trim_copy(raw_arg);

    const std::string chosen =
        command_palette_results[command_palette_selected].insert_text;
    const size_t cmd_pos = parse_body.find(cmd);
    const size_t after_cmd =
        cmd_pos == std::string::npos ? std::string::npos : cmd_pos + cmd.size();
    const bool has_argument_position =
        after_cmd != std::string::npos && after_cmd < parse_body.size() &&
        (parse_body[after_cmd] == ' ' || parse_body[after_cmd] == '\t');
    const bool completing_command = !has_argument_position;

    std::string next_body;
    bool switched_to_argument_completion = false;
    if (completing_command) {
      next_body = chosen;
      if (command_takes_argument(chosen)) {
        next_body += " ";
        switched_to_argument_completion = true;
      }
    } else if (to_lower_copy(cmd) == "rename") {
      std::string rename_args = raw_arg;
      size_t first_non_space = rename_args.find_first_not_of(" \t");
      if (first_non_space == std::string::npos) {
        rename_args.clear();
      } else {
        rename_args.erase(0, first_non_space);
      }
      size_t split = rename_args.find_first_of(" \t");
      if (split == std::string::npos) {
        next_body = cmd + " " + chosen;
      } else {
        std::string left = trim_copy(rename_args.substr(0, split));
        next_body = cmd + " " + left + " " + chosen;
      }
    } else {
      next_body = cmd + " " + chosen;
    }

    command_palette_query = has_colon ? ":" + next_body : next_body;

    // If we just completed a command that expects an argument, the next Tab
    // should complete arguments from the new command context, not keep cycling
    // the old command-prefix candidate list.
    if (switched_to_argument_completion) {
      command_palette_theme_original = command_palette_query;
      command_palette_selected = 0;
    } else {
      command_palette_theme_mode = false;
      command_palette_theme_original.clear();
    }
    refresh_command_palette();
  };

  if (ch == 27) {
    show_command_palette = false;
    command_palette_query.clear();
    command_palette_results.clear();
    reset_completion_state();
    needs_redraw = true;
  } else if (ch == 1008) { // Up
    refresh_command_palette();
    if (!command_palette_results.empty()) {
      command_palette_selected =
          (command_palette_selected - 1 + (int)command_palette_results.size()) %
          (int)command_palette_results.size();
      needs_redraw = true;
    }
  } else if (ch == 1009) { // Down
    refresh_command_palette();
    if (!command_palette_results.empty()) {
      command_palette_selected =
          (command_palette_selected + 1) % (int)command_palette_results.size();
      needs_redraw = true;
    }
  } else if (ch == '\n' || ch == 13) {
    if (trim_copy(command_palette_query).empty() &&
        !command_palette_results.empty()) {
      command_palette_query =
          command_palette_results[std::clamp(command_palette_selected, 0,
                                             (int)command_palette_results.size() - 1)]
              .insert_text;
    }

    std::string line = trim_copy(command_palette_query);
    bool close_prompt = execute_ex_command(line);

    if (close_prompt) {
      show_command_palette = false;
      command_palette_query.clear();
    }
    command_palette_results.clear();
    reset_completion_state();
    needs_redraw = true;
  } else if (ch == '\t' || ch == 9) {
    if (!command_palette_theme_mode) {
      command_palette_theme_mode = true;
      command_palette_theme_original = command_palette_query;
      command_palette_selected = 0;
    }

    refresh_command_palette();
    if (command_palette_results.empty()) {
      set_message("No completion");
      command_palette_results.clear();
      reset_completion_state();
    } else {
      if (command_palette_selected >= (int)command_palette_results.size()) {
        command_palette_selected = 0;
      }
      apply_selected_completion();
      if (!command_palette_results.empty()) {
        command_palette_selected =
            (command_palette_selected + 1) % (int)command_palette_results.size();
      }
    }
    needs_redraw = true;
  } else if (ch == 127 || ch == 8) {
    if (!command_palette_query.empty()) {
      command_palette_query.pop_back();
      reset_completion_state();
      refresh_command_palette();
      needs_redraw = true;
    }
  } else if (ch >= 32 && ch < 127) {
    command_palette_query += ch;
    reset_completion_state();
    refresh_command_palette();
    needs_redraw = true;
  }
}
