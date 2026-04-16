#ifndef TELESCOPE_H
#define TELESCOPE_H

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

struct FileMatch {
    std::string path;
    std::string name;
    int score;
    bool is_directory;
};

class Telescope {
public:
    Telescope();
    
    void open(const std::string& root = "");
    void close();
    bool is_active() const { return active; }
    
    void set_query(const std::string& q);
    void update_results();
    
    void move_up();
    void move_down();
    void select();
    void go_parent();
    
    std::string get_selected_path() const;
    std::vector<std::string> get_preview_lines() const;
    
    const std::vector<FileMatch>& get_results() const { return results; }
    int get_selected_index() const { return selected_index; }
    std::string get_query() const { return query; }
    std::string get_root_dir() const { return root_dir.string(); }
    
    static bool fuzzy_match(const std::string& text, const std::string& pattern);
    static int fuzzy_score(const std::string& text, const std::string& pattern);
    
private:
    bool active;
    std::string query;
    std::vector<FileMatch> results;
    int selected_index;
    fs::path root_dir;
    
    void scan_directory(const fs::path& dir, int depth = 0);
    std::vector<std::string> load_preview(const std::string& path) const;
};

#endif
