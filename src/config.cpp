#include "config.h"
#include "platform_interface.h"
#include "toml.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace orbiter {
namespace fs = std::filesystem;

std::string config_dir() {
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0]) {
    return (fs::path(xdg) / "Orbiter").string();
  }
  const char *home = std::getenv("HOME");
  if (home) {
    return (fs::path(home) / ".config" / "Orbiter").string();
  }
  return "./.config/Orbiter";
}

std::vector<std::string> data_dirs() {
  std::vector<std::string> dirs;
  const char *xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && xdg[0]) {
    dirs.push_back(xdg);
  } else {
    const char *home = std::getenv("HOME");
    if (home) dirs.push_back(std::string(home) + "/.local/share");
  }
  const char *xdg_dirs = std::getenv("XDG_DATA_DIRS");
  if (xdg_dirs && xdg_dirs[0]) {
    std::istringstream ss(xdg_dirs);
    std::string d;
    while (std::getline(ss, d, ':')) {
      if (!d.empty()) dirs.push_back(d);
    }
  } else {
    char pbuf[512];
    if (get_icon_path(pbuf, sizeof(pbuf)) > 0) {
      std::string d = fs::path(pbuf).parent_path().string();
      if (std::find(dirs.begin(), dirs.end(), d) == dirs.end())
        dirs.push_back(d);
    }
    if (get_app_path(pbuf, sizeof(pbuf)) > 0) {
      std::string d = fs::path(pbuf).parent_path().string();
      if (std::find(dirs.begin(), dirs.end(), d) == dirs.end())
        dirs.push_back(d);
    }
  }
  return dirs;
}

std::string cache_dir() {
  const char *xdg = std::getenv("XDG_CACHE_HOME");
  if (xdg && xdg[0]) {
    return (fs::path(xdg) / "orbiter").string();
  }
  const char *home = std::getenv("HOME");
  if (home) {
    return (fs::path(home) / ".cache" / "orbiter").string();
  }
  return "/tmp/orbiter-cache";
}

Config load_config() {
  Config cfg;
  auto path = fs::path(config_dir()) / "config.toml";
  if (!fs::exists(path)) return cfg;

  std::ifstream f(path);
  if (!f.is_open()) return cfg;
  std::stringstream ss;
  ss << f.rdbuf();
  auto tbl = toml::parse(ss.str());

  if (auto v = tbl.get("terminal")) cfg.terminal = *v;
  if (auto v = tbl.get("file_manager")) cfg.file_manager = *v;
  if (auto v = tbl.get("show_metrics")) cfg.show_metrics = (*v == "true");
  if (auto v = tbl.get("max_results")) {
    try { cfg.max_results = std::stoi(*v); } catch (...) {}
  }

  // Load pinned apps from separate file
  auto pinned_path = fs::path(config_dir()) / "pinned.txt";
  if (fs::exists(pinned_path)) {
    std::ifstream pf(pinned_path);
    std::string line;
    while (std::getline(pf, line)) {
      auto trimmed = line;
      auto s = trimmed.find_first_not_of(" \t");
      auto e = trimmed.find_last_not_of(" \t");
      if (s != std::string::npos && e != std::string::npos)
        cfg.pinned_apps.push_back(trimmed.substr(s, e - s + 1));
    }
  }

  return cfg;
}

std::string recent_apps_file() {
  return fs::path(cache_dir()) / "recent.txt";
}

void save_recent_apps(const std::vector<std::string> &recent) {
  auto path = recent_apps_file();
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream f(path);
  for (auto &r : recent) f << r << "\n";
}

std::vector<std::string> load_recent_apps() {
  std::vector<std::string> recent;
  auto path = recent_apps_file();
  if (!fs::exists(path)) return recent;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    auto s = line.find_first_not_of(" \t");
    auto e = line.find_last_not_of(" \t");
    if (s != std::string::npos && e != std::string::npos)
      recent.push_back(line.substr(s, e - s + 1));
  }
  return recent;
}

} // namespace orbiter
