#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <map>

class Config {
private:
    std::map<std::string, std::string> settings;
    std::string config_path;
    
    void load_defaults();
    void parse_line(const std::string& line);
    
public:
    Config();
    void load();
    void save();
    
    std::string get(const std::string& key, const std::string& default_val = "");
    void set(const std::string& key, const std::string& value);
    int get_int(const std::string& key, int default_val = 0);
    bool get_bool(const std::string& key, bool default_val = false);
};

#endif

