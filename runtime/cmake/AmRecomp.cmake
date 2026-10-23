# SPDX-License-Identifier: GPL-3.0-or-later
#
# Liga o C gerado pelo DolRecomp (DOL + 12 RELs) num único alvo, sem editar nenhum arquivo gerado.
#
# Layout esperado de <gerado>:   dol/generated/{generated.h,chunks/*.c}
#                                rels/generated/rels/<nome>_<id>/{generated.h,chunks/*.c}
#
# Cada módulo tem o próprio generated.h com os MESMOS nomes de funções inline (dolrecomp_find_original,
# dolrecomp_call...), então dois módulos nunca entram no mesmo arquivo C. Para cada módulo geramos, no diretório de
# build, um arquivo-ponte (cmake/module_shim.c.in) que inclui SÓ o generated.h dele e registra seus trechos no
# dispatcher. Os símbolos externos (func_<endereço virtual>) já são únicos entre DOL e RELs porque cada REL tem base
# virtual própria (docs/M3_ESQUELETO.md §3).

set(AM_RECOMP_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Descobre os módulos em <dir>; preenche AM_RECOMP_MODULE_IDS e AM_RECOMP_MODULE_DIR_<id>.
function(am_discover_recomp_modules dir)
  set(ids "")
  if(NOT EXISTS "${dir}/dol/generated/generated.h")
    message(FATAL_ERROR "C gerado do DOL não encontrado em ${dir}/dol/generated. Defina AM_GENERATED_DIR "
                        "(ou use -DAM_WITH_GENERATED=OFF).")
  endif()
  list(APPEND ids dol)
  set(AM_RECOMP_MODULE_DIR_dol "${dir}/dol/generated" PARENT_SCOPE)
  file(GLOB rel_dirs LIST_DIRECTORIES true "${dir}/rels/generated/rels/*")
  list(SORT rel_dirs)
  foreach(rel_dir IN LISTS rel_dirs)
    if(EXISTS "${rel_dir}/generated.h")
      get_filename_component(name "${rel_dir}" NAME)
      string(MAKE_C_IDENTIFIER "${name}" id)
      list(APPEND ids "${id}")
      set(AM_RECOMP_MODULE_DIR_${id} "${rel_dir}" PARENT_SCOPE)
    endif()
  endforeach()
  set(AM_RECOMP_MODULE_IDS "${ids}" PARENT_SCOPE)
endfunction()

function(am_add_recomp_target target generated_dir)
  am_discover_recomp_modules("${generated_dir}")
  set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/recomp")
  file(MAKE_DIRECTORY "${out_dir}")

  set(sources "")
  set(decls "")
  set(table "")
  foreach(id IN LISTS AM_RECOMP_MODULE_IDS)
    set(dir "${AM_RECOMP_MODULE_DIR_${id}}")
    file(GLOB chunk_files "${dir}/chunks/*.c")
    list(SORT chunk_files)
    list(APPEND sources ${chunk_files})

    # Início de cada trecho = declarações "void func_XXXXXXXX(CPUState* ctx);" do generated.h do módulo.
    file(STRINGS "${dir}/generated.h" decl_lines REGEX "^void func_[0-9A-Fa-f]+\\(CPUState\\* ctx\\);")
    set(AM_CHUNK_STARTS "")
    foreach(line IN LISTS decl_lines)
      string(REGEX REPLACE "^void func_([0-9A-Fa-f]+).*" "0x\\1U" start "${line}")
      string(APPEND AM_CHUNK_STARTS "        ${start},\n")
    endforeach()
    if(AM_CHUNK_STARTS STREQUAL "")
      message(FATAL_ERROR "${dir}/generated.h sem declarações func_*; formato do DolRecomp mudou?")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dir}/generated.h")

    set(AM_MODULE_ID "${id}")
    set(shim "${out_dir}/mod_${id}.c")
    configure_file("${AM_RECOMP_CMAKE_DIR}/module_shim.c.in" "${shim}" @ONLY)
    # O ponte inclui "generated.h" deste módulo, e só deste.
    set_source_files_properties("${shim}" PROPERTIES INCLUDE_DIRECTORIES "${dir}")
    list(APPEND sources "${shim}")

    string(APPEND decls
      "size_t recomp_mod_${id}_register(void);\nGuestAddr recomp_mod_${id}_entry(void);\n")
    string(APPEND table "    {\"${id}\", recomp_mod_${id}_register, recomp_mod_${id}_entry, 0},\n")
  endforeach()

  set(AM_MODULE_DECLS "${decls}")
  set(AM_MODULE_TABLE "${table}")
  configure_file("${AM_RECOMP_CMAKE_DIR}/recomp_modules.c.in" "${out_dir}/recomp_modules.c" @ONLY)
  list(APPEND sources "${out_dir}/recomp_modules.c")

  add_library(${target} STATIC ${sources})
  target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/runtime/include")
  target_link_libraries(${target} PUBLIC am_ppc)
  # Warnings desligados e -O1: o gerado é ~146 MB de C que não vamos ler (PADROES_DE_CODIGO.md §1).
  target_compile_options(${target} PRIVATE ${AM_GENERATED_FLAGS})
  list(LENGTH AM_RECOMP_MODULE_IDS module_count)
  message(STATUS "C gerado: ${module_count} módulos em ${generated_dir}")
endfunction()
