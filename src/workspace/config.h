#ifndef CONFIG_H
#define CONFIG_H

#include <map>
#include <string>
#include <vector>

class Config
{
private:
  std::map<std::string, std::string> settings;
  // The built-in values, as load_defaults() set them and before settings.conf
  // and the session changed anything. Kept apart from `settings` so a key can
  // answer what it is meant to be (jot.config.info's `default`) after it was
  // changed.
  std::map<std::string, std::string> builtin_defaults;
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
  int get_int(const std::string &key, int default_val = 0) const;
  double get_double(const std::string &key, double default_val = 0.0);
  bool get_bool(const std::string &key, bool default_val = false);
  std::vector<std::string>
  get_list(const std::string &key, char delimiter = ',', bool trim_items = true);
  bool has(const std::string &key) const;
  void unset(const std::string &key);
  // The built-in value of a key, "" when it has none (a key a plugin invented
  // has no default to report).
  std::string default_of(const std::string &key) const;
  // All currently-known keys (defaults + loaded/overridden values).
  std::vector<std::string> keys() const;
  const std::string &path() const
  {
    return config_path;
  }
};

#endif
