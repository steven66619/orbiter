#pragma once
#include <string>
#include <vector>

namespace orbiter {

struct Config {
  std::string terminal = "xterm";
  std::string file_manager = "xdg-open";
  bool show_metrics = false;
  int max_results = 30;
  std::vector<std::string> pinned_apps;
};

Config load_config();
std::string config_dir();
std::string cache_dir();
std::vector<std::string> data_dirs();
std::string recent_apps_file();
void save_recent_apps(const std::vector<std::string> &recent);
std::vector<std::string> load_recent_apps();

} // namespace orbiter
