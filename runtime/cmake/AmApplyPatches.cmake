# SPDX-License-Identifier: GPL-3.0-or-later
# Aplica os patches de uma dependência baixada pelo FetchContent (PATCH_COMMAND), de forma idempotente:
#   cmake -DAM_PATCH_SRC=<fonte> -DAM_PATCH_DIR=<pasta com *.patch> -P AmApplyPatches.cmake
# O carimbo <fonte>/.am_patches guarda o SHA-256 dos patches aplicados. Se bate, não faz nada; se não bate (primeira
# vez, patch novo ou alterado), volta o fonte ao commit da tag (git checkout/clean: é uma cópia baixada, não um
# repositório de trabalho) e aplica todos em ordem. M9 parte 1b: patches/sdl (docs/M9_PARTE1B_SDL.md §4).
cmake_minimum_required(VERSION 3.25)

if(NOT AM_PATCH_SRC OR NOT AM_PATCH_DIR)
  message(FATAL_ERROR "AmApplyPatches: passe -DAM_PATCH_SRC e -DAM_PATCH_DIR")
endif()
find_package(Git REQUIRED)

file(GLOB patches LIST_DIRECTORIES false "${AM_PATCH_DIR}/*.patch")
list(SORT patches)
set(digest "")
foreach(p IN LISTS patches)
  file(SHA256 "${p}" h)
  string(APPEND digest "${h}\n")
endforeach()
string(SHA256 digest_all "${digest}")

set(stamp "${AM_PATCH_SRC}/.am_patches")
if(EXISTS "${stamp}")
  file(READ "${stamp}" old)
  if(old STREQUAL digest_all)
    message(STATUS "AmApplyPatches: ${AM_PATCH_DIR} já aplicado em ${AM_PATCH_SRC}")
    return()
  endif()
endif()

function(am_git)
  execute_process(COMMAND "${GIT_EXECUTABLE}" ${ARGN} WORKING_DIRECTORY "${AM_PATCH_SRC}" RESULT_VARIABLE rc
                  OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "AmApplyPatches: git ${ARGN} falhou em ${AM_PATCH_SRC}:\n${out}${err}")
  endif()
endfunction()

am_git(checkout --force HEAD -- .)
am_git(clean -fdq)
foreach(p IN LISTS patches)
  am_git(apply --whitespace=nowarn "${p}")
  get_filename_component(name "${p}" NAME)
  message(STATUS "AmApplyPatches: ${name} aplicado")
endforeach()
file(WRITE "${stamp}" "${digest_all}")
