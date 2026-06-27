#ifndef PLATFORM_INTERFACE_H
#define PLATFORM_INTERFACE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Read current RX/TX byte counters from the first active network interface.
// Returns 0 on success, -1 on error.
int get_net_speed(unsigned long long *rx_bytes, unsigned long long *tx_bytes);

// Read CPU usage as a percentage (0.0 - 100.0).
// Maintains internal state for delta computation across calls.
// Returns the percentage on success, -1.0 on error.
double get_cpu_usage(void);

// Write the path to the system icon theme directory into buf.
// e.g. "/usr/share/icons" on Linux, "/usr/local/share/icons" on FreeBSD.
// Returns the number of bytes written (excluding NUL), or -1 on error.
int get_icon_path(char *buf, size_t size);

// Write the path to the system applications directory into buf.
// e.g. "/usr/share/applications" on Linux, "/usr/local/share/applications" on FreeBSD.
// Returns the number of bytes written (excluding NUL), or -1 on error.
int get_app_path(char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_INTERFACE_H */
