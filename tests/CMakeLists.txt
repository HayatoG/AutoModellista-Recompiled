# SPDX-License-Identifier: GPL-3.0-or-later
# Um executável por módulo (tests/unit/<modulo>_test.c), todos registrados no CTest.

function(am_add_unit_test name)
  add_executable(${name}_test unit/${name}_test.c)
  target_link_libraries(${name}_test PRIVATE am_core)
  target_include_directories(${name}_test PRIVATE unit)
  am_apply_strict(${name}_test)
  am_apply_sanitize_link(${name}_test)
  add_test(NAME ${name} COMMAND ${name}_test)
endfunction()

am_add_unit_test(mem)
am_add_unit_test(dispatch)
am_add_unit_test(guest_cpu)
am_add_unit_test(dol)
am_add_unit_test(timebase)
am_add_unit_test(trace)
am_add_unit_test(ppc_sys)
am_add_unit_test(profile)
am_add_unit_test(boot)
am_add_unit_test(exception)
am_add_unit_test(hw)
am_add_unit_test(chassis)
am_add_unit_test(sched)
am_add_unit_test(rel_map)
am_add_unit_test(disc)
target_link_libraries(disc_test PRIVATE am_platform) # cria a árvore sintética com platform_make_dir
am_add_unit_test(disc_extract)
target_link_libraries(disc_extract_test PRIVATE am_disc_extract) # cria o mini-disco sintético e extrai
am_add_unit_test(vi)
am_add_unit_test(gx_fifo)
am_add_unit_test(gx_scan)
target_link_libraries(gx_scan_test PRIVATE am_platform) # a thread do GX do teste usa as threads da plataforma
am_add_unit_test(gx_vtx)
am_add_unit_test(gx_xf)
am_add_unit_test(gx_tex)
am_add_unit_test(gx_efb)
am_add_unit_test(gx_xfb)
if(NOT MSVC)
  # A fórmula de referência do teste não pode virar FMA: o runtime compila o gfx com -ffp-contract=off.
  set_source_files_properties(unit/gx_xfb_test.c PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()
am_add_unit_test(gx_render)
am_add_unit_test(gx_gpu)
am_add_unit_test(gpu_scale)
am_add_unit_test(dsp_hle)
am_add_unit_test(ax_voice)
am_add_unit_test(ax_mixer)
am_add_unit_test(audio_dump)
am_add_unit_test(pad_script)
am_add_unit_test(pad_input)
am_add_unit_test(dvd_timing)
am_add_unit_test(memcard)
am_add_unit_test(sha256)
# M9 parte 5: configurações (INI) e banner do disco; a am_front puxa a plataforma, mas os testes não abrem janela.
am_add_unit_test(settings)
target_link_libraries(settings_test PRIVATE am_front)
am_add_unit_test(launcher_gfx)
target_link_libraries(launcher_gfx_test PRIVATE am_front)

# Impasse isolado do jogo: as threads sintéticas esperam umas pelas outras; o chassi tem de parar com o watchdog.
add_test(NAME sched_deadlock COMMAND sched_test deadlock)
set_tests_properties(sched_deadlock PROPERTIES PASS_REGULAR_EXPRESSION
  "\\[FATAL\\] watchdog: pc=0x80009000 despachado 5000 vezes seguidas")

# Os stubs fatais (MMIO, SPR, instrução sem recompilação) têm de parar com mensagem clara. O fatal_test escolhe o
# cenário pelo argumento; PASS_REGULAR_EXPRESSION ignora o código de saída e exige a mensagem.
add_executable(fatal_test unit/fatal_test.c)
target_link_libraries(fatal_test PRIVATE am_core)
target_include_directories(fatal_test PRIVATE unit)
am_apply_strict(fatal_test)
am_apply_sanitize_link(fatal_test)

function(am_add_fatal_test scenario regex)
  add_test(NAME fatal_${scenario} COMMAND fatal_test ${scenario})
  set_tests_properties(fatal_${scenario} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
endfunction()

am_add_fatal_test(mmio_read "\\[FATAL\\] bus: leitura de 4 byte\\(s\\) em 0xCC002100 \\(VI\\)")
am_add_fatal_test(mmio_write "\\[FATAL\\] bus: escrita de 2 byte\\(s\\) em 0xCC006C00 \\(AI\\)")
am_add_fatal_test(unmapped_read "\\[FATAL\\] bus: leitura de 4 byte\\(s\\) em 0x01000000 \\(desconhecido\\)")
am_add_fatal_test(spr_read "\\[FATAL\\] mfspr de SPR 1020 desconhecido")
am_add_fatal_test(spr_write "\\[FATAL\\] mtspr de SPR 1021")
am_add_fatal_test(fallback "\\[FATAL\\] instrução sem recompilação nativa: opcode=0x7C00046C")
am_add_fatal_test(no_function "\\[FATAL\\] dispatcher: nenhuma função recompilada cobre pc=0x80001234")
am_add_fatal_test(aram_oob "\\[FATAL\\] ARAM: acesso")
am_add_fatal_test(watchdog "\\[FATAL\\] watchdog: pc=0x80010000 despachado 100 vezes seguidas")
am_add_fatal_test(icbi_smc "\\[FATAL\\] icbi em 0x80004000 .*código recompilado que mudou")
am_add_fatal_test(bad_bat "\\[FATAL\\] BAT \\(SPR 536/537\\) U=0x90001FFF")
am_add_fatal_test(vector "\\[FATAL\\] vetor de exceção 0x500: palavra em \\+0x0 é 0x7C000000")
am_add_fatal_test(trace_limit "\\[FATAL\\] trace: limite de 40 bytes")
am_add_fatal_test(dsp_ucode "\\[FATAL\\] DSP: microcódigo de hash 0x00000000")
am_add_fatal_test(rel_window "\\[FATAL\\] bus: leitura de 4 byte\\(s\\) em 0x81800000 \\(REL virtual sem módulo ligado\\)")
am_add_fatal_test(gx_opcode "\\[FATAL\\] GX: opcode 0x01 desconhecido na FIFO")
am_add_fatal_test(di_stream "\\[FATAL\\] DI: comando de streaming de áudio 0xE1000000")
am_add_fatal_test(assertion "\\[FATAL\\] asserção falhou")

# M9 parte 1b: o patch da SDL (patches/sdl/) de ponta a ponta numa GPU Metal. Sai com 77 (pulado) sem a SDL remendada
# (AM_SDL3_SYSTEM=ON) ou sem GPU com MSL; docs/M9_PARTE1B_SDL.md §5.
add_executable(sdl_rov_test unit/sdl_rov_test.c)
target_link_libraries(sdl_rov_test PRIVATE am_platform)
target_include_directories(sdl_rov_test PRIVATE unit)
am_apply_strict(sdl_rov_test)
am_apply_sanitize_link(sdl_rov_test)
add_test(NAME sdl_rov COMMAND sdl_rov_test)
set_tests_properties(sdl_rov PROPERTIES SKIP_RETURN_CODE 77)

# M9 parte 1b: o ubershader em HLSL dá o mesmo EFB que o MSL à mão num lote sintético (docs/M9_PARTE1B_HLSL.md).
# Sai com 77 (pulado) sem os dois caminhos.
add_executable(gpu_shader_test unit/gpu_shader_test.c)
target_link_libraries(gpu_shader_test PRIVATE am_platform)
target_include_directories(gpu_shader_test PRIVATE unit)
am_apply_strict(gpu_shader_test)
am_apply_sanitize_link(gpu_shader_test)
add_test(NAME gpu_shader COMMAND gpu_shader_test)
set_tests_properties(gpu_shader PROPERTIES SKIP_RETURN_CODE 77)
