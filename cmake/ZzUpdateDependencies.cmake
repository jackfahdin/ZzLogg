include_guard(GLOBAL)

# Apply only inside update/updater directories. Old configured build trees can
# retain /MD in their cache even with CMP0091 NEW. Remove those legacy directory
# flags and let each target's explicit property select its CRT. Never mutate the
# cache or the parent's application flags.
macro(zzlogg_update_clean_legacy_crt_flags)
  if(MSVC)
    foreach(language IN ITEMS C CXX)
      foreach(configuration IN ITEMS "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
        string(REGEX REPLACE "(^| )[/-]M[DT]d?( |$)" " "
          CMAKE_${language}_FLAGS${configuration} "${CMAKE_${language}_FLAGS${configuration}}")
      endforeach()
    endforeach()
  endif()
endmacro()
function(zzlogg_update_runtime target static_crt)
  if(static_crt)
    set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  else()
    set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()
endfunction()
zzlogg_update_clean_legacy_crt_flags()

set(_zz_update_vendor "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/vendor")
add_library(zzlogg_update_json INTERFACE)
target_include_directories(zzlogg_update_json SYSTEM INTERFACE
  "${_zz_update_vendor}/nlohmann-json")
function(zzlogg_add_update_monocypher target static_crt)
  add_library(${target} STATIC
    "${_zz_update_vendor}/monocypher/src/monocypher.c"
    "${_zz_update_vendor}/monocypher/src/optional/monocypher-ed25519.c")
  target_include_directories(${target} PUBLIC
    "${_zz_update_vendor}/monocypher/src"
    "${_zz_update_vendor}/monocypher/src/optional")
  set_target_properties(${target} PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED YES POSITION_INDEPENDENT_CODE ON)
  zzlogg_update_runtime(${target} ${static_crt})
endfunction()
zzlogg_add_update_monocypher(zzlogg_update_monocypher FALSE)
