file(READ "${SOURCE_ROOT}/.github/workflows/ci-build.yml" ci)
file(READ "${SOURCE_ROOT}/.github/workflows/codeql-analysis.yml" codeql)

foreach(required IN ITEMS
    "gcc-13" "g++-13"
    "CC: gcc-13" "CXX: g++-13"
    "compilerversioncontracttest.cmake")
  string(FIND "${ci}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Formal Linux CI does not enforce GCC >=13.1: ${required}")
  endif()
  string(FIND "${codeql}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "CodeQL does not enforce GCC >=13.1: ${required}")
  endif()
endforeach()
