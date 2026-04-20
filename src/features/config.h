#ifndef CONFIG_H
#define CONFIG_H

#include <map>
#include <string>
#include <vector>

class Config {
private:
  std::map<std::string, std::string> settings;
  std::string config_path;

  void load_defaults();
  void parse_line(const std::string &line);

public:
  Config();
  void load();
  void save();

  std::string get(const std::string &key, const std::string &default_val = "");
  void set(const std::string &key, const std::string &value);
  void set_int(const std::string &key, int value);
  void set_bool(const std::string &key, bool value);
  int get_int(const std::string &key, int default_val = 0);
  double get_double(const std::string &key, double default_val = 0.0);
  bool get_bool(const std::string &key, bool default_val = false);
  std::vector<std::string> get_list(const std::string &key, char delimiter = ',',
                                    bool trim_items = true);
  bool has(const std::string &key) const;
  void unset(const std::string &key);
};

#endif
