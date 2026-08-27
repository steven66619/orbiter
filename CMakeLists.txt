cmake_minimum_required(VERSION 3.20)
project(orbiter
  VERSION 1.0.4
  DESCRIPTION "Application launcher with system icons"
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# --- FREEBSD PATH INJECTION START ---
if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  # Direct compiler to the local header and library dirs
  include_directories(/usr/local/include)
  link_directories(/usr/local/lib)

  # Inject FreeBSD paths into the pkg-config environment so CMake modules find them
  set(ENV{PKG_CONFIG_PATH} "/usr/local/libdata/pkgconfig:/usr/local/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()
# --- FREEBSD PATH INJECTION END ---

find_package(PkgConfig REQUIRED)

pkg_check_modules(WAYLAND REQUIRED wayland-client)
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(PANGO REQUIRED pangocairo)
pkg_check_modules(RSVG REQUIRED librsvg-2.0)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)

# --- Wayland protocol codegen ---
find_program(WAYLAND_SCANNER wayland-scanner)
if(NOT WAYLAND_SCANNER)
  message(FATAL_ERROR "wayland-scanner not found")
endif()

set(WLR_XML ${CMAKE_SOURCE_DIR}/wlr-layer-shell-unstable-v1.xml)

# xdg-shell XML ships with wayland-protocols
execute_process(
  COMMAND pkg-config --variable=pkgdatadir wayland-protocols
  OUTPUT_VARIABLE WP_PKGDATADIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT WP_PKGDATADIR)
  set(WP_PKGDATADIR /usr/share/wayland-protocols)
endif()
set(XDG_XML ${WP_PKGDATADIR}/stable/xdg-shell/xdg-shell.xml)
if(NOT EXISTS ${XDG_XML})
  message(FATAL_ERROR "xdg-shell.xml not found at ${XDG_XML}")
endif()

set(WLR_HEADER ${CMAKE_BINARY_DIR}/wlr-layer-shell-unstable-v1-client.h)
set(WLR_CODE ${CMAKE_BINARY_DIR}/wlr-layer-shell-unstable-v1-client.c)
set(XDG_HEADER ${CMAKE_BINARY_DIR}/xdg-shell-client.h)
set(XDG_CODE ${CMAKE_BINARY_DIR}/xdg-shell-client.c)

# The generated header uses `namespace` as a parameter name, which is a C++
# keyword; rename it so the header can be included from C++.
add_custom_command(
  OUTPUT ${WLR_HEADER}
  COMMAND ${WAYLAND_SCANNER} client-header ${WLR_XML} ${WLR_HEADER}.tmp
  COMMAND sed s/namespace/wl_namespace/g ${WLR_HEADER}.tmp > ${WLR_HEADER}
  COMMAND rm -f ${WLR_HEADER}.tmp
  DEPENDS ${WLR_XML}
  COMMENT "Generating wlr-layer-shell client header"
)
add_custom_command(
  OUTPUT ${WLR_CODE}
  COMMAND ${WAYLAND_SCANNER} private-code ${WLR_XML} ${WLR_CODE}
  DEPENDS ${WLR_XML}
  COMMENT "Generating wlr-layer-shell client code"
)
add_custom_command(
  OUTPUT ${XDG_HEADER}
  COMMAND ${WAYLAND_SCANNER} client-header ${XDG_XML} ${XDG_HEADER}
  DEPENDS ${XDG_XML}
  COMMENT "Generating xdg-shell client header"
)
add_custom_command(
  OUTPUT ${XDG_CODE}
  COMMAND ${WAYLAND_SCANNER} private-code ${XDG_XML} ${XDG_CODE}
  DEPENDS ${XDG_XML}
  COMMENT "Generating xdg-shell client code"
)

if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
  set(PLATFORM_SRC src/platform_freebsd.c)
else()
  set(PLATFORM_SRC src/platform_linux.c)
endif()

add_executable(orbiter
  src/main.cpp
  src/config.cpp
  src/theme.cpp
  src/launch.cpp
  src/metrics.cpp
  src/icons.cpp
  src/desktop.cpp
  src/window.cpp
  ${PLATFORM_SRC}
  ${WLR_HEADER}
  ${WLR_CODE}
  ${XDG_HEADER}
  ${XDG_CODE}
)

target_include_directories(orbiter PRIVATE
  ${WAYLAND_INCLUDE_DIRS}
  ${CAIRO_INCLUDE_DIRS}
  ${PANGO_INCLUDE_DIRS}
  ${RSVG_INCLUDE_DIRS}
  ${XKBCOMMON_INCLUDE_DIRS}
  ${CMAKE_BINARY_DIR}
  src
)

target_link_libraries(orbiter PRIVATE
  ${WAYLAND_LIBRARIES}
  ${CAIRO_LIBRARIES}
  ${PANGO_LIBRARIES}
  ${RSVG_LIBRARIES}
  ${XKBCOMMON_LIBRARIES}
  m
)

target_compile_options(orbiter PRIVATE
  ${WAYLAND_CFLAGS_OTHER}
  ${CAIRO_CFLAGS_OTHER}
  ${PANGO_CFLAGS_OTHER}
  ${RSVG_CFLAGS_OTHER}
  ${XKBCOMMON_CFLAGS_OTHER}
)

install(TARGETS orbiter DESTINATION bin)