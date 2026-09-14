# Resolve application runtime packaging independently of the framework linkage.
# Based on the vendored runtime resolver; keep its compiler/license checks.
if(ZZ_BUNDLE_GNU_RUNTIME)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
       OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
       OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(FATAL_ERROR
            "ZZ_BUNDLE_GNU_RUNTIME requires Linux, GNU, and Release")
    endif()
    if(NOT IS_ABSOLUTE "${ZZ_GNU_RUNTIME_LICENSE_DIR}"
       OR NOT IS_DIRECTORY "${ZZ_GNU_RUNTIME_LICENSE_DIR}")
        message(FATAL_ERROR
            "ZZ_GNU_RUNTIME_LICENSE_DIR must be an absolute existing directory")
    endif()
    foreach(zz_runtime_license IN ITEMS COPYING3 COPYING.RUNTIME)
        set(zz_runtime_license_path
            "${ZZ_GNU_RUNTIME_LICENSE_DIR}/${zz_runtime_license}")
        if(NOT EXISTS "${zz_runtime_license_path}"
           OR IS_DIRECTORY "${zz_runtime_license_path}")
            message(FATAL_ERROR
                "GNU runtime license is absent: ${zz_runtime_license_path}")
        endif()
        file(SIZE "${zz_runtime_license_path}" zz_runtime_license_size)
        if(zz_runtime_license_size EQUAL 0)
            message(FATAL_ERROR
                "GNU runtime license is empty: ${zz_runtime_license_path}")
        endif()
    endforeach()

    foreach(zz_runtime_name IN ITEMS libstdc++.so.6 libgcc_s.so.1)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}"
                "-print-file-name=${zz_runtime_name}"
            RESULT_VARIABLE zz_runtime_result
            OUTPUT_VARIABLE zz_runtime_candidate
            ERROR_VARIABLE zz_runtime_error
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT zz_runtime_result EQUAL 0
           OR "${zz_runtime_candidate}" STREQUAL ""
           OR "${zz_runtime_candidate}" STREQUAL "${zz_runtime_name}"
           OR NOT EXISTS "${zz_runtime_candidate}"
           OR IS_DIRECTORY "${zz_runtime_candidate}")
            message(FATAL_ERROR
                "Unable to resolve ${zz_runtime_name} from ${CMAKE_CXX_COMPILER}: ${zz_runtime_error}")
        endif()
        file(REAL_PATH "${zz_runtime_candidate}" zz_runtime_real_path)
        if(NOT EXISTS "${zz_runtime_real_path}"
           OR IS_DIRECTORY "${zz_runtime_real_path}")
            message(FATAL_ERROR
                "Resolved GNU runtime is not a regular file: ${zz_runtime_real_path}")
        endif()
        if(zz_runtime_name STREQUAL "libstdc++.so.6")
            set(ZZ_GNU_LIBSTDCXX_PATH "${zz_runtime_real_path}")
        else()
            set(ZZ_GNU_LIBGCC_PATH "${zz_runtime_real_path}")
        endif()
    endforeach()
endif()
