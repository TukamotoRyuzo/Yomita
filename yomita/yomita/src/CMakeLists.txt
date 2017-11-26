cmake_minimum_required(VERSION 2.8)
set(CMAKE_CXX_COMPILER "clang++")
add_definitions("-std=c++14 -fno-exceptions -fno-rtti -Wextra -Ofast -MMD -MP")
add_definitions("-Wno-unused-parameter")
set(CMAKE_EXE_LINKER_FLAGS "-Wl,-s -v")

if(NOT DEFINED target)
   set(target "avx2")
endif()
if(target STREQUAL avx2)
  add_definitions("-DNDEBUG -DHAVE_BMI2 -DHAVE_SSE4 -mbmi2 -mavx2 -march=corei7-avx")
elseif(target STREQUAL sse42)
  add_definitions("-DNDEBUG -DHAVE_SSE42 -msse4.2 -march=corei7")
elseif(target STREQUAL learn)
  add_definitions("-DHAVE_BMI2 -DLEARN -DHAVE_SSE4 -mbmi2 -mavx2 -march=corei7-avx")
elseif(target STREQUAL learn-sse42)
  add_definitions("-DHAVE_SSE42 -DLEARN -mbmi2 -mavx2 -march=corei7-avx")
elseif(DEFINED target)
  message(FATAL_ERROR "invalid target")
endif()

set(sources
  benchmark.cpp
  bitboard.cpp
  bitboard.h
  bitop.h
  board.cpp
  board.h
  book.cpp
  book.h
  byteboard.cpp
  byteboard.h
  common.cpp
  common.h
  config.h
  enumoperator.h
  eval_kppt.cpp
  eval_ppt.cpp
  eval_pptp.cpp
  eval_util.cpp
  evalsum.h
  evaluate.h
  genmove.cpp
  haffman.cpp
  hand.h
  learn.cpp
  learn.h
  main.cpp
  move.h
  movepick.cpp
  movepick.h
  platform.h
  pretty.cpp
  progress.cpp
  progress.h
  range.h
  search.cpp
  search.h
  sfen_rw.cpp
  sfen_rw.h
  test.cpp
  thread.cpp
  thread.h
  timeman.cpp
  timeman.h
  tt.cpp
  tt.h
  types.h
  usi.cpp
  usi.h
  usioption.cpp
)
add_executable(Yomita-by-clang ${sources})
target_link_libraries(Yomita-by-clang stdc++fs pthread)

