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

am_add_fatal_test(mmio_read "\\[FATAL\\] bus: leitura de 4 byte\\(s\\) em 0xCC002000 \\(VI\\)")
am_add_fatal_test(mmio_write "\\[FATAL\\] bus: escrita de 2 byte\\(s\\) em 0xCC006C00 \\(AI\\)")
am_add_fatal_test(unmapped_read "\\[FATAL\\] bus: leitura de 4 byte\\(s\\) em 0x01000000 \\(desconhecido\\)")
am_add_fatal_test(spr_read "\\[FATAL\\] mfspr de SPR 1008 desconhecido")
am_add_fatal_test(spr_write "\\[FATAL\\] mtspr de SPR 1009")
am_add_fatal_test(fallback "\\[FATAL\\] instrução sem recompilação nativa: opcode=0x7C002FAC")
am_add_fatal_test(no_function "\\[FATAL\\] dispatcher: nenhuma função recompilada cobre pc=0x80001234")
am_add_fatal_test(aram_oob "\\[FATAL\\] ARAM: acesso")
am_add_fatal_test(assertion "\\[FATAL\\] asserção falhou")
