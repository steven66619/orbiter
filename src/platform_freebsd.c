#include "platform_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_mib.h>
#include <ifaddrs.h>

// ── Network ────────────────────────────────────────────────────────────

int get_net_speed(unsigned long long *rx_bytes, unsigned long long *tx_bytes) {
  int mib[6] = {CTL_NET, PF_LINK, NETLINK_GENERIC, IFMIB_SYSTEM, IFMIB_IFCOUNT};
  int ifcount;
  size_t len = sizeof(ifcount);

  if (sysctl(mib, 5, &ifcount, &len, NULL, 0) < 0)
    return -1;

  for (int i = 1; i <= ifcount; i++) {
    struct ifmibdata ifmd;
    mib[3] = IFMIB_IFDATA;
    mib[4] = i;
    mib[5] = IFDATA_GENERAL;
    len = sizeof(ifmd);

    if (sysctl(mib, 6, &ifmd, &len, NULL, 0) < 0)
      continue;

    if (ifmd.ifmd_flags & IFF_LOOPBACK)
      continue;

    *rx_bytes = ifmd.ifmd_data.ifi_ibytes;
    *tx_bytes = ifmd.ifmd_data.ifi_obytes;
    return 0;
  }

  return -1;
}

// ── CPU ───────────────────────────────────────────────────────────────

#define CPUSTATES 5
#define CP_IDLE   4

double get_cpu_usage(void) {
  static long prev[CPUSTATES];
  static int first = 1;

  long cur[CPUSTATES];
  size_t len = sizeof(cur);

  if (sysctlbyname("kern.cp_time", &cur, &len, NULL, 0) < 0)
    return -1.0;

  if (first) {
    first = 0;
    memcpy(prev, cur, sizeof(cur));
    return 0.0;
  }

  long total_cur = 0, total_prev = 0;
  for (int i = 0; i < CPUSTATES; i++) {
    total_cur += cur[i];
    total_prev += prev[i];
  }

  long dtotal = total_cur - total_prev;
  long didle  = cur[CP_IDLE] - prev[CP_IDLE];

  memcpy(prev, cur, sizeof(cur));

  if (dtotal <= 0) return 0.0;
  return 100.0 * (1.0 - (double)didle / (double)dtotal);
}

// ── Path helpers ───────────────────────────────────────────────────────

static int dir_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// ── Icon path ──────────────────────────────────────────────────────────

int get_icon_path(char *buf, size_t size) {
  const char *xdg = getenv("XDG_DATA_DIRS");
  if (xdg && xdg[0]) {
    char copy[1024];
    strncpy(copy, xdg, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    const char *sep = ":";
    char *save;
    char *dir = strtok_r(copy, sep, &save);
    while (dir) {
      char path[1024];
      snprintf(path, sizeof(path), "%s/icons", dir);
      if (dir_exists(path)) {
        snprintf(buf, size, "%s", path);
        return (int)strlen(buf);
      }
      dir = strtok_r(NULL, sep, &save);
    }
  }

  static const char *fallbacks[] = {
    "/usr/local/share/icons",
    NULL
  };

  for (int i = 0; fallbacks[i]; i++) {
    if (dir_exists(fallbacks[i])) {
      snprintf(buf, size, "%s", fallbacks[i]);
      return (int)strlen(buf);
    }
  }

  snprintf(buf, size, "%s", "/usr/local/share/icons");
  return (int)strlen(buf);
}

// ── App path ───────────────────────────────────────────────────────────

int get_app_path(char *buf, size_t size) {
  const char *xdg = getenv("XDG_DATA_DIRS");
  if (xdg && xdg[0]) {
    char copy[1024];
    strncpy(copy, xdg, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    const char *sep = ":";
    char *save;
    char *dir = strtok_r(copy, sep, &save);
    while (dir) {
      char path[1024];
      snprintf(path, sizeof(path), "%s/applications", dir);
      if (dir_exists(path)) {
        snprintf(buf, size, "%s", path);
        return (int)strlen(buf);
      }
      dir = strtok_r(NULL, sep, &save);
    }
  }

  static const char *fallbacks[] = {
    "/usr/local/share/applications",
    NULL
  };

  for (int i = 0; fallbacks[i]; i++) {
    if (dir_exists(fallbacks[i])) {
      snprintf(buf, size, "%s", fallbacks[i]);
      return (int)strlen(buf);
    }
  }

  snprintf(buf, size, "%s", "/usr/local/share/applications");
  return (int)strlen(buf);
}
