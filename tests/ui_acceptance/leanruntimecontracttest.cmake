if(NOT IS_DIRECTORY "${RUNTIME_DIR}")
  message(FATAL_ERROR "RUNTIME_DIR must exist")
endif()
foreach(redundant IN ITEMS
    COPYING NOTICE README.md DOCUMENTATION.md
    opengl32sw.dll D3Dcompiler_47.dll dxcompiler.dll dxil.dll
    generic styles)
  if(EXISTS "${RUNTIME_DIR}/${redundant}")
    message(FATAL_ERROR "Redundant runtime component: ${redundant}")
  endif()
endforeach()
file(GLOB pdf_libraries "${RUNTIME_DIR}/Qt6Pdf*.dll")
if(pdf_libraries)
  message(FATAL_ERROR "Unused PDF runtime must not be deployed: ${pdf_libraries}")
endif()
if(CONFIG STREQUAL "Debug")
  set(suffix d)
endif()
foreach(plugin IN ITEMS qpdf qgif qicns qtga qtiff qwbmp qwebp)
  if(EXISTS "${RUNTIME_DIR}/imageformats/${plugin}${suffix}.dll")
    message(FATAL_ERROR "Unused image plugin: ${plugin}")
  endif()
endforeach()
foreach(required IN ITEMS
    ZzLogg.exe licenses/ZzLogg/COPYING licenses/ZzLogg/NOTICE
    licenses/ZzPureTools/LICENSE
    platforms/qwindows${suffix}.dll iconengines/qsvgicon${suffix}.dll
    imageformats/qsvg${suffix}.dll imageformats/qjpeg${suffix}.dll
    imageformats/qico${suffix}.dll tls/qschannelbackend${suffix}.dll)
  if(NOT EXISTS "${RUNTIME_DIR}/${required}")
    message(FATAL_ERROR "Required runtime component is missing: ${required}")
  endif()
endforeach()
