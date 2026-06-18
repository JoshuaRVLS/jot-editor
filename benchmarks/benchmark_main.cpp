#include "features/folding.h"
#include "in_memory_line_provider.h"
#include "lazy_line_provider.h"
#include "tools/symbols/index.h"
#include "tools/workspace/search.h"
#include "ui/text.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
using Clock = std::chrono::steady_clock;

struct BenchmarkCase {
  std::string name;
  int iterations = 1;
  std::function<void()> fn;
};

struct BenchmarkResult {
  std::string name;
  int iterations = 0;
  double min_ms = 0.0;
  double avg_ms = 0.0;
  double max_ms = 0.0;
};

class TempDir {
public:
  explicit TempDir(const std::string &name) {
    auto base = fs::temp_directory_path();
    auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     Clock::now().time_since_epoch())
                     .count();
    path_ = base / (name + "_" + std::to_string(stamp));
    fs::create_directories(path_);
  }

  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }

  const fs::path &path() const { return path_; }

private:
  fs::path path_;
};

std::vector<std::string> make_cpp_lines(int functions) {
  std::vector<std::string> lines;
  lines.reserve(functions * 6);
  for (int i = 0; i < functions; i++) {
    lines.push_back("int function_" + std::to_string(i) + "() {");
    lines.push_back("  if (value_" + std::to_string(i) + " > 0) {");
    lines.push_back("    return value_" + std::to_string(i) + ";");
    lines.push_back("  }");
    lines.push_back("  return 0;");
    lines.push_back("}");
  }
  return lines;
}

std::vector<std::string> make_python_lines(int classes) {
  std::vector<std::string> lines;
  lines.reserve(classes * 5);
  for (int i = 0; i < classes; i++) {
    lines.push_back("class Class" + std::to_string(i) + ":");
    lines.push_back("    def method_" + std::to_string(i) + "(self):");
    lines.push_back("        value = " + std::to_string(i));
    lines.push_back("        return value");
    lines.push_back("");
  }
  return lines;
}

void write_lines(const fs::path &path, const std::vector<std::string> &lines) {
  std::ofstream out(path);
  if (!out.is_open()) {
    throw std::runtime_error("failed to create " + path.string());
  }
  for (const auto &line : lines) {
    out << line << '\n';
  }
}

BenchmarkResult run_case(const BenchmarkCase &bench) {
  if (bench.iterations <= 0) {
    throw std::runtime_error("benchmark iterations must be positive");
  }

  bench.fn();

  std::vector<double> samples;
  samples.reserve(bench.iterations);
  for (int i = 0; i < bench.iterations; i++) {
    auto start = Clock::now();
    bench.fn();
    auto end = Clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    samples.push_back(elapsed.count());
  }

  auto minmax = std::minmax_element(samples.begin(), samples.end());
  double total = std::accumulate(samples.begin(), samples.end(), 0.0);
  return {bench.name, bench.iterations, *minmax.first,
          total / (double)samples.size(), *minmax.second};
}

std::vector<BenchmarkCase> make_benchmarks() {
  auto cpp_lines = make_cpp_lines(12000);
  auto python_lines = make_python_lines(9000);
  std::string long_ascii_line(4096, 'x');
  return {
      {"in_memory_line_provider_random_reads", 8,
       [cpp_lines]() {
         InMemoryLineProvider provider(cpp_lines);
         std::size_t checksum = 0;
         for (int i = 0; i < 200000; i++) {
           checksum += provider.get_line((i * 37) % provider.line_count()).size();
         }
         if (checksum == 0) {
           throw std::runtime_error("unexpected empty checksum");
         }
       }},
      {"lazy_line_provider_index_and_reads", 5,
       [cpp_lines]() {
         TempDir temp("jot_bench_lazy");
         fs::path file = temp.path() / "large.cpp";
         write_lines(file, cpp_lines);
         auto provider = LazyLineProvider::open(file.string());
         if (!provider) {
           throw std::runtime_error("failed to open lazy provider");
         }
         std::size_t checksum = 0;
         for (int i = 0; i < 50000; i++) {
           checksum += provider->get_line((i * 97) % provider->line_count()).size();
         }
         if (checksum == 0) {
           throw std::runtime_error("unexpected empty checksum");
         }
       }},
      {"folding_detect_cpp_ranges", 10,
       [cpp_lines]() {
         auto ranges = Folding::detect_ranges(cpp_lines, ".cpp");
         if (ranges.empty()) {
           throw std::runtime_error("expected cpp fold ranges");
         }
       }},
      {"folding_visible_navigation", 12,
       [cpp_lines]() {
         auto ranges = Folding::detect_ranges(cpp_lines, ".cpp");
         for (std::size_t i = 0; i < ranges.size(); i += 3) {
           ranges[i].collapsed = true;
         }
         int line = 0;
         for (int i = 0; i < 10000; i++) {
           line = Folding::advance_visible_lines(
               ranges, line, 1, (int)cpp_lines.size());
         }
         if (line < 0) {
           throw std::runtime_error("invalid visible line");
         }
       }},
      {"ui_text_cell_count_long_lines", 20,
       [long_ascii_line]() {
         int total = 0;
         for (int i = 0; i < 20000; i++) {
           total += ui_cell_count(long_ascii_line);
         }
         if (total <= 0) {
           throw std::runtime_error("unexpected text count");
         }
       }},
      {"symbol_index_python_document", 15,
       [python_lines]() {
         auto symbols = SymbolIndex::extract_document_symbols(python_lines,
                                                              "module.py");
         if (symbols.empty()) {
           throw std::runtime_error("expected python symbols");
         }
       }},
      {"workspace_search_temp_project", 5,
       [cpp_lines]() {
         TempDir temp("jot_bench_search");
         for (int i = 0; i < 40; i++) {
           fs::path subdir = temp.path() / ("dir_" + std::to_string(i % 8));
           fs::create_directories(subdir);
           write_lines(subdir / ("file_" + std::to_string(i) + ".cpp"),
                       cpp_lines);
         }
         auto results = WorkspaceSearch::search(temp.path().string(),
                                                "function_11999", 4 * 1024 * 1024,
                                                2000);
         if (results.empty()) {
           throw std::runtime_error("expected workspace search results");
         }
       }},
  };
}
} // namespace

int main() {
  try {
    auto benches = make_benchmarks();
    std::vector<BenchmarkResult> results;
    results.reserve(benches.size());

    std::cout << "jot benchmark suite\n";
    std::cout << "cases: " << benches.size() << "\n\n";

    for (const auto &bench : benches) {
      auto result = run_case(bench);
      results.push_back(result);
      std::cout << std::left << std::setw(36) << result.name << "  "
                << "iters=" << std::setw(3) << result.iterations << "  "
                << std::fixed << std::setprecision(3)
                << "min=" << std::setw(9) << result.min_ms << "ms  "
                << "avg=" << std::setw(9) << result.avg_ms << "ms  "
                << "max=" << std::setw(9) << result.max_ms << "ms\n";
    }

    double total_avg = 0.0;
    for (const auto &result : results) {
      total_avg += result.avg_ms;
    }
    std::cout << "\nsummary_avg_ms=" << std::fixed << std::setprecision(3)
              << total_avg << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "benchmark failed: " << e.what() << "\n";
    return 1;
  }
}
