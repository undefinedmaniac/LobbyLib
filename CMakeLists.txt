cmake_minimum_required(VERSION 3.21)

project(
  GameLobby
  VERSION 1.0
  DESCRIPTION "A game lobby system for simple network games"
  LANGUAGES CXX)

# set(BUILD_SHARED_LIBS OFF)
# set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.77)
message(STATUS "Boost version: ${Boost_VERSION}")

add_subdirectory(src)
add_subdirectory(apps)
