# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(AutoModellistaRecompiled VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# M9 parte 5: C++ só para o Dear ImGui e para os arquivos que chamam a API dele (runtime/platform/platform_ui.cpp e
# runtime/front/*.cpp); sem exceções nem RTTI.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Opções (docs/M3_ESQUELETO.md) ---------------------------------------------------------------------------------
option(AM_WITH_GENERATED "Liga o C gerado pelo DolRecomp (DOL + RELs). OFF: só stub, usado no CI" ON)
set(AM_GENERATED_DIR "$ENV{AM_GENERATED_DIR}" CACHE PATH
    "Diretório que contém dol/generated e rels/generated/rels/* (fora do repositório)")
set(AM_DOLRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/DolRecomp" CACHE PATH
    "DolRecomp no commit fixo de scripts/fetch_tools.sh (precisa de src/cpu/cpu.h e cpu.c)")
option(AM_SDL3_SYSTEM "Usa a SDL3 instalada (find_package) em vez de baixar a tag fixa via FetchContent" OFF)
option(AM_SDL3_SHARED "SDL3 do FetchContent como biblioteca compartilhada (pacote do Linux: libSDL3.so.0 ao lado do executável, rpath $ORIGIN; docs/M9_PARTE3A_LINUX.md)" OFF)
option(AM_BUILD_TESTS "Compila os testes unitários (CTest)" ON)
option(AM_SANITIZE "ASan + UBSan nos alvos da runtime (builds Debug)" OFF)

set(AM_SDL3_TAG "release-3.4.18" CACHE STRING "Tag fixa da SDL3 usada pelo FetchContent")
set(AM_IMGUI_TAG "v1.92.9" CACHE STRING "Tag fixa do Dear ImGui (MIT) usada pelo FetchContent (M9 parte 5)")

if(NOT EXISTS "${AM_DOLRECOMP_DIR}/src/cpu/cpu.h")
  message(FATAL_ERROR "DolRecomp não encontrado em ${AM_DOLRECOMP_DIR}. Rode scripts/fetch_tools.sh "
                      "ou passe -DAM_DOLRECOMP_DIR=<caminho>.")
endif()

# --- Flags ---------------------------------------------------------------------------------------------------------
# clang-cl usa /clang:<flag> para repassar flags no estilo GNU.
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(AM_STRICT_FLAGS /clang:-Wall /clang:-Wextra /clang:-Wpedantic /clang:-Wshadow /clang:-Wconversion
                      /clang:-Wstrict-prototypes /clang:-Werror)
  set(AM_STRICT_CXX_FLAGS /clang:-Wall /clang:-Wextra /clang:-Wpedantic /clang:-Wshadow /clang:-Wconversion
                          /clang:-Werror /GR- /EHs-c-)
  set(AM_CXX_LEAN_FLAGS /GR- /EHs-c-)
  set(AM_GENERATED_FLAGS /w /clang:-O1 /clang:-g0)
  set(AM_SANITIZE_FLAGS /fsanitize=address)
else()
  set(AM_STRICT_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes -Werror)
  set(AM_STRICT_CXX_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -fno-exceptions -fno-rtti)
  set(AM_CXX_LEAN_FLAGS -fno-exceptions -fno-rtti -fno-threadsafe-statics)
  set(AM_GENERATED_FLAGS -w -O1 -g0)
  set(AM_SANITIZE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# M9 parte 5: a am_core chega ao executável por dois caminhos (am_recomp e am_front); o ld do Xcode avisa da
# repetição, que é inofensiva.
if(APPLE)
  add_link_options(LINKER:-no_warn_duplicate_libraries)
endif()

# Alvos com código nosso: warnings estritos (PADROES_DE_CODIGO.md §1).
function(am_apply_strict target)
  target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:${AM_STRICT_FLAGS}>
                                           $<$<COMPILE_LANGUAGE:CXX>:${AM_STRICT_CXX_FLAGS}>)
  if(AM_SANITIZE)
    target_compile_options(${target} PRIVATE ${AM_SANITIZE_FLAGS})
  endif()
endfunction()

# Executáveis precisam do runtime dos sanitizers no link.
function(am_apply_sanitize_link target)
  if(AM_SANITIZE)
    target_link_options(${target} PRIVATE ${AM_SANITIZE_FLAGS})
  endif()
endfunction()

add_subdirectory(runtime)

if(AM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
