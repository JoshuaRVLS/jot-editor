#ifndef CPP_ASSIST_H
#define CPP_ASSIST_H

#include <filesystem>
#include <string>
#include <vector>

namespace CppAssist {
namespace fs = std::filesystem;

struct MethodDecl {
  std::string namespace_name;
  std::string class_name;
  std::string return_type;
  std::string name;
  std::string params;
  std::string suffix;
  bool constructor = false;
  bool destructor = false;
};

struct GenerateResult {
  std::string source_text;
  int generated_count = 0;
  bool created_source = false;
  std::string header_path;
  std::string source_path;
};

bool is_header_path(const fs::path &path);
bool is_source_path(const fs::path &path);
fs::path counterpart_path_for(const fs::path &path);
std::string include_guard_for(const fs::path &path);
std::string header_skeleton(const fs::path &path);
std::string source_skeleton(const fs::path &header_path);
std::vector<MethodDecl> parse_method_declarations(const std::string &header_text);
std::string generate_definition(const MethodDecl &decl);
GenerateResult generate_missing_implementations(const std::string &header_text,
                                                const std::string &source_text,
                                                const fs::path &header_path,
                                                const fs::path &source_path,
                                                bool source_exists);

} // namespace CppAssist

#endif
