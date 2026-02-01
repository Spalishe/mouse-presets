cmake_minimum_required(VERSION 3.10)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(mouse_presets)

set(CMAKE_C_STANDARD 17)

set(CMAKE_C_STANDARD_REQUIRED True)

file(GLOB SOURCES "src/*.c")

add_executable(mouse_presets ${SOURCES})

set(CMAKE_C_FLAGS_DEBUG "-lm -lwayland-client -lcairo -O3 -march=native -g")
set(CMAKE_C_FLAGS_RELEASE "-lm -lwayland-client -lcairo -O3 -march=native")

# target_include_directories(mouse_presets PRIVATE src)

