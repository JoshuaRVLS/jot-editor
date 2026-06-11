#include "cpp_assist.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>

namespace CppAssist {
namespace {
std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

std::string trim_copy(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

std::string collapse_spaces(const std::string &s) {
  std::string out;
  bool in_space = false;
  for (char c : s) {
    if (std::isspace((unsigned char)c)) {
      if (!in_space) {
        out.push_back(' ');
        in_space = true;
      }
    } else {
      out.push_back(c);
      in_space = false;
    }
  }
  return trim_copy(out);
}

bool contains_word(const std::string &s, const std::string &word) {
  std::regex re("(^|[^A-Za-z0-9_])" + word + "([^A-Za-z0-9_]|$)");
  return std::regex_search(s, re);
}

std::string strip_comments(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  bool line_comment = false;
  bool block_comment = false;
  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    char next = (i + 1 < text.size()) ? text[i + 1] : '\0';
    if (line_comment) {
      if (c == '\n') {
        line_comment = false;
        out.push_back(c);
      } else {
        out.push_back(' ');
      }
      continue;
    }
    if (block_comment) {
      if (c == '*' && next == '/') {
        block_comment = false;
        out += "  ";
        i++;
      } else {
        out.push_back(c == '\n' ? '\n' : ' ');
      }
      continue;
    }
    if (c == '/' && next == '/') {
      line_comment = true;
      out += "  ";
      i++;
      continue;
    }
    if (c == '/' && next == '*') {
      block_comment = true;
      out += "  ";
      i++;
      continue;
    }
    out.push_back(c);
  }
  return out;
}

std::string remove_default_values(const std::string &params) {
  std::string out;
  int paren = 0;
  int angle = 0;
  bool skipping = false;
  for (size_t i = 0; i < params.size(); i++) {
    char c = params[i];
    if (c == '(') paren++;
    if (c == ')' && paren > 0) paren--;
    if (c == '<') angle++;
    if (c == '>' && angle > 0) angle--;
    if (!skipping && c == '=' && paren == 0 && angle == 0) {
      skipping = true;
      continue;
    }
    if (skipping && c == ',' && paren == 0 && angle == 0) {
      skipping = false;
      out.push_back(c);
      continue;
    }
    if (!skipping) {
      out.push_back(c);
    }
  }
  return collapse_spaces(out);
}

std::string method_key(const MethodDecl &decl) {
  std::string key;
  if (!decl.namespace_name.empty()) {
    key += decl.namespace_name + "::";
  }
  key += decl.class_name + "::" + decl.name + "(";
  return key;
}

bool source_contains_method(const std::string &source, const MethodDecl &decl) {
  std::string qualified = method_key(decl);
  if (source.find(qualified) != std::string::npos) {
    return true;
  }
  std::string class_only = decl.class_name + "::" + decl.name + "(";
  return source.find(class_only) != std::string::npos;
}

std::string basename_no_ext(const fs::path &path) {
  return path.stem().string();
}

std::string guard_token(std::string s) {
  std::string out;
  for (char c : s) {
    if (std::isalnum((unsigned char)c)) {
      out.push_back((char)std::toupper((unsigned char)c));
    } else {
      out.push_back('_');
    }
  }
  while (out.find("__") != std::string::npos) {
    out = std::regex_replace(out, std::regex("__+"), "_");
  }
  return out;
}
} // namespace

bool is_header_path(const fs::path &path) {
  const std::string ext = lower_copy(path.extension().string());
  return ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx";
}

bool is_source_path(const fs::path &path) {
  const std::string ext = lower_copy(path.extension().string());
  return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx";
}

fs::path counterpart_path_for(const fs::path &path) {
  fs::path out = path;
  if (is_header_path(path)) {
    out.replace_extension(".cpp");
  } else if (is_source_path(path)) {
    out.replace_extension(".hpp");
  }
  return out;
}

std::string include_guard_for(const fs::path &path) {
  return guard_token(path.filename().string()) + "_";
}

std::string header_skeleton(const fs::path &path) {
  std::string guard = include_guard_for(path);
  std::string class_name = basename_no_ext(path);
  if (!class_name.empty()) {
    class_name[0] = (char)std::toupper((unsigned char)class_name[0]);
  }
  return "#ifndef " + guard + "\n#define " + guard + "\n\nclass " +
         class_name + " {\npublic:\n    " + class_name + "();\n};\n\n#endif\n";
}

std::string source_skeleton(const fs::path &header_path) {
  return "#include \"" + header_path.filename().string() + "\"\n";
}

std::vector<MethodDecl> parse_method_declarations(const std::string &header_text) {
  std::vector<MethodDecl> out;
  std::string text = strip_comments(header_text);
  std::vector<std::string> namespace_stack;
  std::vector<std::string> class_stack;
  std::vector<int> brace_stack;
  std::stringstream ss(text);
  std::string raw_line;
  std::string statement;
  int brace_depth = 0;

  std::regex namespace_re("^\\s*namespace\\s+([A-Za-z_][A-Za-z0-9_:]*)\\s*\\{");
  std::regex class_re("^\\s*(class|struct)\\s+([A-Za-z_][A-Za-z0-9_]*)[^;{]*\\{");
  std::regex method_re(
      "^\\s*(?:(virtual|static|inline|constexpr|explicit)\\s+)*"
      "([^();{}=]+?)?\\s*"
      "([~A-Za-z_][A-Za-z0-9_]*)\\s*\\((.*)\\)\\s*"
      "(const\\s*)?(noexcept\\s*)?(override\\s*)?(final\\s*)?;$");

  while (std::getline(ss, raw_line)) {
    std::string line = trim_copy(raw_line);
    std::smatch match;
    if (line == "public:" || line == "private:" || line == "protected:") {
      continue;
    }
    bool structural_line = false;
    if (std::regex_search(line, match, namespace_re)) {
      namespace_stack.push_back(match[1].str());
      brace_stack.push_back(brace_depth);
      structural_line = true;
    }
    if (std::regex_search(line, match, class_re)) {
      class_stack.push_back(match[2].str());
      brace_stack.push_back(brace_depth);
      structural_line = true;
    }

    if (!structural_line && !line.empty() && line[0] != '#') {
      statement += " " + line;
    }

    for (char c : line) {
      if (c == '{') {
        brace_depth++;
      } else if (c == '}') {
        brace_depth--;
        while (!brace_stack.empty() && brace_depth <= brace_stack.back()) {
          if (!class_stack.empty()) {
            class_stack.pop_back();
          } else if (!namespace_stack.empty()) {
            namespace_stack.pop_back();
          }
          brace_stack.pop_back();
        }
      }
    }

    if (statement.find(';') == std::string::npos) {
      continue;
    }

    std::string stmt = collapse_spaces(statement);
    statement.clear();
    if (class_stack.empty() || stmt.find('(') == std::string::npos ||
        stmt.find(')') == std::string::npos) {
      continue;
    }
    if (stmt.find('{') != std::string::npos || stmt.find("typedef") != std::string::npos ||
        stmt.find("using ") != std::string::npos || stmt.find("operator") != std::string::npos ||
        stmt.find("template") != std::string::npos || stmt.find("= 0") != std::string::npos ||
        stmt.find("=0") != std::string::npos || stmt.find("= default") != std::string::npos ||
        stmt.find("= delete") != std::string::npos) {
      continue;
    }

    const std::string current_class = class_stack.back();
    std::regex ctor_re("^\\s*(?:explicit\\s+)?(~?" + current_class +
                       ")\\s*\\((.*)\\)\\s*"
                       "(const\\s*)?(noexcept\\s*)?(override\\s*)?(final\\s*)?;$");
    if (std::regex_match(stmt, match, ctor_re)) {
      MethodDecl decl;
      decl.class_name = current_class;
      decl.namespace_name.clear();
      for (size_t i = 0; i < namespace_stack.size(); i++) {
        if (i > 0) decl.namespace_name += "::";
        decl.namespace_name += namespace_stack[i];
      }
      decl.name = match[1].str();
      decl.params = remove_default_values(match[2].str());
      decl.suffix = collapse_spaces(match[3].str() + match[4].str());
      if (!decl.suffix.empty()) {
        decl.suffix = " " + decl.suffix;
      }
      decl.constructor = decl.name == decl.class_name;
      decl.destructor = decl.name == "~" + decl.class_name;
      out.push_back(std::move(decl));
      continue;
    }

    if (!std::regex_match(stmt, match, method_re)) {
      continue;
    }

    MethodDecl decl;
    decl.class_name = current_class;
    decl.namespace_name.clear();
    for (size_t i = 0; i < namespace_stack.size(); i++) {
      if (i > 0) decl.namespace_name += "::";
      decl.namespace_name += namespace_stack[i];
    }
    decl.return_type = collapse_spaces(match[2].str());
    decl.name = match[3].str();
    decl.params = remove_default_values(match[4].str());
    decl.suffix = collapse_spaces(match[5].str() + match[6].str());
    if (!decl.suffix.empty()) {
      decl.suffix = " " + decl.suffix;
    }
    decl.constructor = decl.name == decl.class_name;
    decl.destructor = decl.name == "~" + decl.class_name;
    if (!decl.constructor && !decl.destructor && decl.return_type.empty()) {
      continue;
    }
    if (contains_word(decl.return_type, "friend")) {
      continue;
    }
    out.push_back(std::move(decl));
  }

  return out;
}

std::string generate_definition(const MethodDecl &decl) {
  std::string signature;
  if (!decl.constructor && !decl.destructor) {
    signature += decl.return_type + " ";
  }
  signature += decl.class_name + "::" + decl.name + "(" + decl.params + ")" +
               decl.suffix;
  std::string body = signature + " {\n";
  if (!decl.constructor && !decl.destructor && decl.return_type != "void") {
    body += "  return {};\n";
  }
  body += "}\n";
  if (!decl.namespace_name.empty()) {
    body = "namespace " + decl.namespace_name + " {\n\n" + body + "\n} // namespace " +
           decl.namespace_name + "\n";
  }
  return body;
}

GenerateResult generate_missing_implementations(const std::string &header_text,
                                                const std::string &source_text,
                                                const fs::path &header_path,
                                                const fs::path &source_path,
                                                bool source_exists) {
  GenerateResult result;
  result.header_path = header_path.string();
  result.source_path = source_path.string();
  result.created_source = !source_exists;
  result.source_text = source_exists ? source_text : source_skeleton(header_path);

  std::vector<MethodDecl> declarations = parse_method_declarations(header_text);
  std::vector<std::string> generated;
  std::set<std::string> seen;
  for (const auto &decl : declarations) {
    std::string key = method_key(decl);
    if (!seen.insert(key).second) {
      continue;
    }
    if (source_contains_method(result.source_text, decl)) {
      continue;
    }
    generated.push_back(generate_definition(decl));
  }

  if (!generated.empty()) {
    if (!result.source_text.empty() && result.source_text.back() != '\n') {
      result.source_text.push_back('\n');
    }
    result.source_text += "\n// Generated implementations\n\n";
    for (const auto &definition : generated) {
      result.source_text += definition;
      if (!result.source_text.empty() && result.source_text.back() != '\n') {
        result.source_text.push_back('\n');
      }
      result.source_text.push_back('\n');
    }
  }
  result.generated_count = (int)generated.size();
  return result;
}

} // namespace CppAssist
