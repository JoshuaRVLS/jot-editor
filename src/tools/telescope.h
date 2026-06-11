#ifndef TELESCOPE_H
#define TELESCOPE_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct FileMatch {
    std::string path;
    std::string name;
    std::string relative_path;
    std::string parent_path;
    int score;
    bool is_directory;
};

struct TelescopePreview {
    std::vector<std::string> lines;
    std::string title;
    std::string detail;
    std::uintmax_t size_bytes = 0;
    bool is_directory = false;
    bool is_binary = false;
    bool skipped = false;
};

class TaskQueue;

class Telescope {
public:
    Telescope();
    
    void open(const std::string& root = "");
    void close();
    bool is_active() const { return active; }
    
    void set_query(const std::string& q, TaskQueue *tq = nullptr);
    void update_results();

    void scan_async(TaskQueue *tq);
    void cancel_scan();
    void apply_results(std::vector<FileMatch> new_results);
    
    void move_up();
    void move_down();
    void select();
    void go_parent();
    
    std::string get_selected_path() const;
    std::string get_selected_relative_path() const;
    TelescopePreview get_selected_preview() const;
    std::vector<std::string> get_preview_lines() const;
    
    const std::vector<FileMatch>& get_results() const { return results; }
    int get_result_count() const { return (int)results.size(); }
    int get_selected_index() const { return selected_index; }
    std::string get_query() const { return query; }
    std::string get_root_dir() const { return root_dir.string(); }
    int current_scan_id() const { return scan_id_.load(); }
    
    static bool fuzzy_match(const std::string& text, const std::string& pattern);
    static int fuzzy_score(const std::string& text, const std::string& pattern);
    
private:
    bool active;
    std::string query;
    std::vector<FileMatch> results;
    int selected_index;
    fs::path root_dir;

    std::atomic<int> scan_id_{0};
    mutable bool preview_cache_valid = false;
    mutable std::string preview_cache_path;
    mutable TelescopePreview preview_cache;
    
    void scan_directory(const fs::path& dir, int depth = 0);
    void invalidate_preview_cache();
    TelescopePreview load_preview(const FileMatch& match) const;
};

#endif
