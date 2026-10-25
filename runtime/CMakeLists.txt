# SPDX-License-Identifier: GPL-3.0-or-later

# --- am_ppc: semântica PowerPC (ponto flutuante, paired singles, exceções) --------------------------------------------
# Compila o cpu.c do DolRecomp (GPL-3.0, mesmo commit do gerador) em vez de reescrever ~1700 linhas de FP do Gekko.
# O cpu.c também define mem_read*/mem_write* sobre cpu->ram e ppc_mftb sobre cpu->timebase (que nada avança); esses nomes
# são da NOSSA runtime (core/mem.c, core/timebase.c), então no cpu.c eles são renomeados por macro. Warnings desligados:
# é código de terceiros.
add_library(am_ppc STATIC "${AM_DOLRECOMP_DIR}/src/cpu/cpu.c")
target_include_directories(am_ppc SYSTEM PUBLIC "${AM_DOLRECOMP_DIR}/src")
target_compile_definitions(am_ppc PRIVATE
  mem_read8=dolrecomp_mem_read8   mem_read16=dolrecomp_mem_read16
  mem_read32=dolrecomp_mem_read32 mem_read64=dolrecomp_mem_read64
  mem_write8=dolrecomp_mem_write8   mem_write16=dolrecomp_mem_write16
  mem_write32=dolrecomp_mem_write32 mem_write64=dolrecomp_mem_write64
  ppc_mftb=dolrecomp_ppc_mftb)
target_compile_options(am_ppc PRIVATE ${AM_GENERATED_FLAGS})

# --- gfx: o GX em software (M6). Sem contração de a*b+c em FMA: arm64 e x86_64 dão os mesmos pixels (o golden do
# M6 guarda o hash da captura dos dois builds).
set(AM_GFX_SOURCES gfx/gx_fifo.c gfx/gx_vtx.c gfx/gx_xf.c gfx/gx_bp.c gfx/gx_tex.c gfx/gx_tev.c gfx/gx_efb.c
  gfx/gx_setup.c gfx/gx_raster.c gfx/gx_gpu.c)
if(NOT MSVC)
  set_source_files_properties(${AM_GFX_SOURCES} PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

# --- am_core: memória, CPUState, dispatcher, boot e chassi; os/ (exceções) e hw/ (dispositivos MMIO) ---------------
add_library(am_core STATIC
  core/log.c core/mem.c core/dispatch.c core/guest_cpu.c
  core/timebase.c core/ppc_sys.c core/dol.c core/boot.c core/trace.c core/chassis.c core/disc.c core/rel_map.c core/hook.c
  core/pad_script.c core/pad_input.c core/profile.c
  os/exception.c os/os_link.c os/os_threads.c
  hw/bus.c hw/ai.c hw/pi.c hw/mi.c hw/dsp.c hw/di.c hw/dvd_timing.c hw/si.c hw/exi.c hw/exi_card.c hw/memcard.c hw/vi.c
  hw/cp.c hw/pe.c hw/gather_pipe.c
  ${AM_GFX_SOURCES}
  audio/dsp_hle.c audio/ax_voice.c audio/ax_mixer.c audio/audio_dump.c)
target_include_directories(am_core PUBLIC include)
target_link_libraries(am_core PUBLIC am_ppc)
if(NOT APPLE AND NOT WIN32)
  target_link_libraries(am_core PUBLIC m) # hw/dvd_timing.c e gfx/: sqrt, fmod, pow
endif()
am_apply_strict(am_core)

# --- am_recomp: o C gerado (DOL + RELs) ------------------------------------------------------------------------------
include(cmake/AmRecomp.cmake)
if(AM_WITH_GENERATED)
  am_add_recomp_target(am_recomp "${AM_GENERATED_DIR}")
  target_link_libraries(am_recomp PUBLIC am_core)
else()
  add_library(am_recomp INTERFACE)
  target_sources(am_recomp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/core/recomp_stub.c")
  target_link_libraries(am_recomp INTERFACE am_core)
endif()

# --- am_platform: SDL3 ----------------------------------------------------------------------------------------------
if(AM_SDL3_SYSTEM)
  find_package(SDL3 REQUIRED CONFIG)
else()
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  # M9 parte 1b: patches/sdl (texturas de storage graváveis no fragment shader, na ordem da rasterização;
  # docs/M9_PARTE1B_SDL.md). O hash dos patches entra no comando para que mudar um patch refaça o FetchContent; o
  # script é idempotente. Com FETCHCONTENT_SOURCE_DIR_SDL3 (fonte local) nada é aplicado.
  file(GLOB AM_SDL3_PATCHES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/patches/sdl/*.patch")
  list(SORT AM_SDL3_PATCHES)
  set(AM_SDL3_PATCH_DIGEST "")
  foreach(p IN LISTS AM_SDL3_PATCHES)
    file(SHA256 "${p}" h)
    string(APPEND AM_SDL3_PATCH_DIGEST "${h}\n")
  endforeach()
  string(SHA256 AM_SDL3_PATCH_DIGEST "${AM_SDL3_PATCH_DIGEST}")
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${AM_SDL3_TAG}
    GIT_SHALLOW TRUE
    PATCH_COMMAND ${CMAKE_COMMAND} -DAM_PATCH_SRC=<SOURCE_DIR> -DAM_PATCH_DIR=${PROJECT_SOURCE_DIR}/patches/sdl
                  -DAM_PATCH_DIGEST=${AM_SDL3_PATCH_DIGEST} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AmApplyPatches.cmake
    SYSTEM)
  FetchContent_MakeAvailable(SDL3)
endif()

add_library(am_platform STATIC platform/platform.c platform/platform_gpu.c)
target_include_directories(am_platform PUBLIC include)
target_link_libraries(am_platform PUBLIC SDL3::SDL3)
am_apply_strict(am_platform)

# --- Executável -----------------------------------------------------------------------------------------------------
add_executable(automodellista main.c game/game_run.c game/game_diag.c)
target_link_libraries(automodellista PRIVATE am_recomp am_platform)
am_apply_strict(automodellista)
am_apply_sanitize_link(automodellista)
