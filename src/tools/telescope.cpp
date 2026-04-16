#include "telescope.h"
#include <algorithm>
#include <fstream>
#include <cctype>

Telescope::Telescope() {
    active = false;
    selected_index = 0;
    root_dir = fs::current_path();
}

void Telescope::open(const std::string& root) {
    active = true;
    if (!root.empty()) {
        root_dir = fs::path(root);
    } else if (!fs::exists(root_dir)) {
        root_dir = fs::current_path();
    }
    query = "";
    selected_index = 0;
    results.clear();
    update_results();
}

void Telescope::close() {
    active = false;
    query = "";
    results.clear();
    selected_index = 0;
}

void Telescope::set_query(const std::string& q) {
    query = q;
    update_results();
}

void Telescope::update_results() {
    results.clear();
    
    if (query.empty()) {
        scan_directory(root_dir, 0);
    } else {
        scan_directory(root_dir, 0);
        
        std::vector<FileMatch> filtered;
        for (auto& match : results) {
            if (fuzzy_match(match.name, query) || fuzzy_match(match.path, query)) {
                match.score = fuzzy_score(match.name, query);
                filtered.push_back(match);
            }
        }
        
        std::sort(filtered.begin(), filtered.end(), [](const FileMatch& a, const FileMatch& b) {
            if (a.is_directory != b.is_directory) {
                return a.is_directory;
            }
            return a.score > b.score;
        });
        
        results = filtered;
    }
    
    if (selected_index >= (int)results.size()) {
        selected_index = std::max(0, (int)results.size() - 1);
    }
    if (selected_index < 0) selected_index = 0;
}

void Telescope::scan_directory(const fs::path& dir, int depth) {
    if (depth > 2) return;
    
    try {
        for (const auto& entry : fs::directory_iterator(dir)) {
            FileMatch match;
            match.path = entry.path().string();
            match.name = entry.path().filename().string();
            match.is_directory = fs::is_directory(entry);
            match.score = 0;
            
            if (match.name[0] == '.') continue;
            
            results.push_back(match);
            
            if (match.is_directory && depth < 1) {
                scan_directory(entry.path(), depth + 1);
            }
        }
    } catch (...) {
    }
}

void Telescope::move_up() {
    if (selected_index > 0) selected_index--;
}

void Telescope::move_down() {
    if (selected_index < (int)results.size() - 1) selected_index++;
}

void Telescope::select() {
    if (selected_index >= 0 && selected_index < (int)results.size()) {
        if (results[selected_index].is_directory) {
            root_dir = results[selected_index].path;
            query.clear();
            selected_index = 0;
            update_results();
        }
    }
}

void Telescope::go_parent() {
    if (root_dir.has_parent_path()) {
        root_dir = root_dir.parent_path();
        query.clear();
        selected_index = 0;
        update_results();
    }
}

std::string Telescope::get_selected_path() const {
    if (selected_index >= 0 && selected_index < (int)results.size()) {
        return results[selected_index].path;
    }
    return "";
}

std::vector<std::string> Telescope::get_preview_lines() const {
    std::string path = get_selected_path();
    if (path.empty() || results[selected_index].is_directory) {
        return {};
    }
    return load_preview(path);
}

std::vector<std::string> Telescope::load_preview(const std::string& path) const {
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (file.is_open()) {
        std::string line;
        int count = 0;
        while (std::getline(file, line) && count < 50) {
            if (line.length() > 200) {
                line = line.substr(0, 200) + "...";
            }
            lines.push_back(line);
            count++;
        }
        file.close();
    }
    return lines;
}

bool Telescope::fuzzy_match(const std::string& text, const std::string& pattern) {
    if (pattern.empty()) return true;
    
    std::string text_lower = text;
    std::string pattern_lower = pattern;
    std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
    std::transform(pattern_lower.begin(), pattern_lower.end(), pattern_lower.begin(), ::tolower);
    
    size_t pattern_idx = 0;
    for (size_t i = 0; i < text_lower.length() && pattern_idx < pattern_lower.length(); i++) {
        if (text_lower[i] == pattern_lower[pattern_idx]) {
            pattern_idx++;
        }
    }
    
    return pattern_idx == pattern_lower.length();
}

int Telescope::fuzzy_score(const std::string& text, const std::string& pattern) {
    if (pattern.empty()) return 100;
    
    std::string text_lower = text;
    std::string pattern_lower = pattern;
    std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
    std::transform(pattern_lower.begin(), pattern_lower.end(), pattern_lower.begin(), ::tolower);
    
    if (text_lower.find(pattern_lower) != std::string::npos) {
        return 100;
    }
    
    int score = 0;
    size_t pattern_idx = 0;
    size_t last_match = 0;
    
    for (size_t i = 0; i < text_lower.length() && pattern_idx < pattern_lower.length(); i++) {
        if (text_lower[i] == pattern_lower[pattern_idx]) {
            if (i == last_match + 1) {
                score += 10;
            } else {
                score += 5;
            }
            last_match = i;
            pattern_idx++;
        }
    }
    
    if (pattern_idx == pattern_lower.length()) {
        return score;
    }
    
    return 0;
}
