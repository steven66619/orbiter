#include "launch.h"
#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

namespace orbiter {

bool launch_background(const std::string &command, const std::string &stratum) {
  auto trimmed = command;
  auto notspace = [](unsigned char c) { return !std::isspace(c); };
  auto start = std::find_if(trimmed.begin(), trimmed.end(), notspace);
  auto end = std::find_if(trimmed.rbegin(), trimmed.rend(), notspace).base();
  if (start >= end) return false;
  trimmed = std::string(start, end);

  // Double-fork: the grandchild is orphaned (adopted by init) so it
  // survives when orbiter exits, without calling setsid() which would
  // break Wayland/GTK display connections.
  // stdio is redirected to /dev/null — NOT closed, because GTK/Wayland
  // apps crash if file descriptors 0-2 don't exist.
  pid_t pid = fork();
  if (pid == 0) {
    pid_t pid2 = fork();
    if (pid2 == 0) {
      // Grandchild: redirect stdio to /dev/null, then exec
      int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) close(devnull);
      }
      if (!stratum.empty()) {
        std::string inner = "exec " + trimmed;
        execl("/bedrock/bin/strat", "strat", stratum.c_str(), "sh", "-c", inner.c_str(), nullptr);
      } else {
        execl("/bin/sh", "sh", "-c", trimmed.c_str(), nullptr);
      }
      _exit(127);
    }
    // First child: exit immediately so grandchild is orphaned
    _exit(0);
  }
  // Reap the first child so it doesn't zombie
  int status;
  waitpid(pid, &status, 0);
  return true;
}

} // namespace orbiter
