cmake_minimum_required(VERSION 3.16)
project(gbemu C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(gbemu
  src/main.c
  src/gb.c
  src/cpu.c
  src/mmu.c
  src/ppu.c
  src/timer.c
  src/cart.c
  src/apu.c
  src/util.c
)

target_include_directories(gbemu PRIVATE include)

find_package(SDL2 CONFIG QUIET)
if (TARGET SDL2::SDL2)
  target_link_libraries(gbemu PRIVATE SDL2::SDL2)
  if (TARGET SDL2::SDL2main)
    target_link_libraries(gbemu PRIVATE SDL2::SDL2main)
  endif()
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
  target_include_directories(gbemu PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(gbemu PRIVATE ${SDL2_LIBRARIES})
  target_compile_options(gbemu PRIVATE ${SDL2_CFLAGS_OTHER})
endif()

if (MSVC)
  target_compile_options(gbemu PRIVATE /W4)
else()
  target_compile_options(gbemu PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()
