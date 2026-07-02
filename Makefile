PREFIX ?= /usr/local
BUILD_DIR ?= build
NPROCS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

release:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(NPROCS)

debug:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) -j$(NPROCS)

install: release
	install -Dm755 $(BUILD_DIR)/runrs $(DESTDIR)$(PREFIX)/bin/runrs

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/runrs

clean:
	rm -rf $(BUILD_DIR)

.PHONY: release debug install uninstall clean

