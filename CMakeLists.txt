cmake_minimum_required(VERSION 4.0)
project(Launcher)

find_package(PkgConfig REQUIRED)
pkg_check_modules(libgio2  REQUIRED IMPORTED_TARGET
  gio-2.0
  gio-unix-2.0
  gobject-2.0
  glib-2.0
  )

add_library(imgui)
target_include_directories(imgui PUBLIC imgui)
target_sources(imgui PRIVATE
  imgui/imgui.cpp
  imgui/imgui_demo.cpp
  imgui/imgui_draw.cpp
  imgui/imgui_tables.cpp
  imgui/imgui_widgets.cpp
  imgui/misc/cpp/imgui_stdlib.cpp
  imgui/backends/imgui_impl_sdl3.cpp
  imgui/backends/imgui_impl_sdlrenderer3.cpp
  )

add_executable(launcher)
target_compile_options(launcher PUBLIC -std=c++26)
target_sources(launcher PUBLIC main.cpp)
target_link_libraries(launcher PUBLIC SDL3 imgui PkgConfig::libgio2)
