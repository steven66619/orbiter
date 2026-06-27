#define _GNU_SOURCE
#include "platform_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// ── Network ────────────────────────────────────────────────────────────

int get_net_speed(unsigned long long *rx_bytes, unsigned long long *tx_bytes) {
  FILE *f = fopen("/proc/net/dev", "r");
  if (!f) return -1;

  char line[512];
  if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
  if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }

  while (fgets(line, sizeof(line), f)) {
    char *p = line;
    while (*p == ' ') ++p;
    char *colon = strchr(p, ':');
    if (!colon) continue;
    *colon = '\0';

    // skip loopback
    if (strcmp(p, "lo") == 0) continue;

    unsigned long long rx = 0, tx = 0;
    if (sscanf(colon + 1, " %llu %*u %*u %*u %*u %*u %*u %*u %llu",
               &rx, &tx) >= 2) {
      *rx_bytes = rx;
      *tx_bytes = tx;
      fclose(f);
      return 0;
    }
  }

  fclose(f);
  return -1;
}

// ── CPU ───────────────────────────────────────────────────────────────

double get_cpu_usage(void) {
  static unsigned long long prev_idle = 0, prev_total = 0;
  static int first = 1;

  FILE *f = fopen("/proc/stat", "r");
  if (!f) return -1.0;

  char line[256];
  if (!fgets(line, sizeof(line), f)) { fclose(f); return -1.0; }
  fclose(f);

  unsigned long long user, nice, sys, idle, iowait, irq, softirq, steal;
  if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
             &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal) < 4)
    return -1.0;

  unsigned long long total = user + nice + sys + idle + iowait + irq + softirq + steal;

  if (first) {
    first = 0;
    prev_idle = idle;
    prev_total = total;
    return 0.0;
  }

  unsigned long long dtotal = total - prev_total;
  unsigned long long didle  = idle - prev_idle;

  prev_idle  = idle;
  prev_total = total;

  if (dtotal == 0) return 0.0;
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
    "/usr/share/icons",
    NULL
  };

  for (int i = 0; fallbacks[i]; i++) {
    if (dir_exists(fallbacks[i])) {
      snprintf(buf, size, "%s", fallbacks[i]);
      return (int)strlen(buf);
    }
  }

  snprintf(buf, size, "%s", "/usr/share/icons");
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
    "/usr/share/applications",
    NULL
  };

  for (int i = 0; fallbacks[i]; i++) {
    if (dir_exists(fallbacks[i])) {
      snprintf(buf, size, "%s", fallbacks[i]);
      return (int)strlen(buf);
    }
  }

  snprintf(buf, size, "%s", "/usr/share/applications");
  return (int)strlen(buf);
}
