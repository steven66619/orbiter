#include "window.h"
#include <iostream>

int main() {
  orbiter::LauncherWindow app;
  if (!app.init()) {
    std::cerr << "Failed to initialize launcher" << std::endl;
    return 1;
  }
  app.run();
  return 0;
}
