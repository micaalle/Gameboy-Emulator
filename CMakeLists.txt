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

if (NOT TARGET SDL2::SDL2 AND NOT TARGET SDL2::SDL2-static AND NOT TARGET SDL2)
  include(FetchContent)

  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_SDL2MAIN OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-2.30.10.zip
  )
  FetchContent_MakeAvailable(SDL2)
endif()

if (TARGET SDL2::SDL2)
  target_link_libraries(gbemu PRIVATE SDL2::SDL2)
  set(GBEMU_SDL_TARGET SDL2::SDL2)
elseif (TARGET SDL2::SDL2-static)
  target_link_libraries(gbemu PRIVATE SDL2::SDL2-static)
  set(GBEMU_SDL_TARGET SDL2::SDL2-static)
elseif (TARGET SDL2)
  target_link_libraries(gbemu PRIVATE SDL2)
  set(GBEMU_SDL_TARGET SDL2)
else()
  message(FATAL_ERROR "SDL2 was not found and could not be fetched. :(")
endif()

if (TARGET SDL2::SDL2main)
  target_link_libraries(gbemu PRIVATE SDL2::SDL2main)
endif()

if (WIN32 AND DEFINED GBEMU_SDL_TARGET)
  add_custom_command(TARGET gbemu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:${GBEMU_SDL_TARGET}>
      $<TARGET_FILE_DIR:gbemu>
    VERBATIM
  )
endif()

if (MSVC)
  target_compile_options(gbemu PRIVATE /W4)
else()
  target_compile_options(gbemu PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()
