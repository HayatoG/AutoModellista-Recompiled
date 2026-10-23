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

# --- am_core: memória, CPUState, dispatcher, boot e chassi; os/ (exceções) e hw/ (dispositivos MMIO) ---------------
add_library(am_core STATIC
  core/log.c core/mem.c core/dispatch.c core/guest_cpu.c
  core/timebase.c core/ppc_sys.c core/dol.c core/boot.c core/trace.c core/chassis.c
  os/exception.c
  hw/bus.c hw/ai.c hw/pi.c hw/mi.c hw/dsp.c hw/di.c hw/si.c hw/exi.c hw/vi.c)
target_include_directories(am_core PUBLIC include)
target_link_libraries(am_core PUBLIC am_ppc)
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
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${AM_SDL3_TAG}
    GIT_SHALLOW TRUE
    SYSTEM)
  FetchContent_MakeAvailable(SDL3)
endif()

add_library(am_platform STATIC platform/platform.c)
target_include_directories(am_platform PUBLIC include)
target_link_libraries(am_platform PUBLIC SDL3::SDL3)
am_apply_strict(am_platform)

# --- Executável -----------------------------------------------------------------------------------------------------
add_executable(automodellista main.c)
target_link_libraries(automodellista PRIVATE am_recomp am_platform)
am_apply_strict(automodellista)
am_apply_sanitize_link(automodellista)
