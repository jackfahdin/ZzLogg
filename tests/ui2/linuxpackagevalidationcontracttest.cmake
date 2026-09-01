file(READ "${SOURCE_ROOT}/.github/workflows/ci-build.yml" ci)

string(FIND "${ci}" "  Linux:" linux_position)
string(FIND "${ci}" "  Mac:" mac_position)
if(linux_position EQUAL -1 OR mac_position EQUAL -1
   OR linux_position GREATER mac_position)
  message(FATAL_ERROR "Cannot isolate formal Linux CI job")
endif()
math(EXPR linux_length "${mac_position} - ${linux_position}")
string(SUBSTRING "${ci}" ${linux_position} ${linux_length} linux_job)

string(FIND "${linux_job}" "- uses: ./.github/actions/klogg-version" version_position)
string(FIND "${linux_job}" "- name: Configure" configure_position)
if(version_position EQUAL -1 OR configure_position EQUAL -1
   OR version_position GREATER configure_position)
  message(FATAL_ERROR "Linux packaging must establish KLOGG_VERSION before configure")
endif()

foreach(required IN ITEMS
    [=[ZzLogg-${KLOGG_VERSION}-Linux.deb]=]
    [=[ZzLogg-${KLOGG_VERSION}-Linux.rpm]=]
    "dpkg-deb -f"
    "rpm -qp"
    "docker run --rm"
    "ubuntu:22.04"
    "fedora:41"
    "unset QT_ROOT_DIR QTDIR LD_LIBRARY_PATH QT_PLUGIN_PATH"
    "/opt/ZzLogg/bin/ZzLogg"
    "/opt/ZzLogg/lib/libQt6Core.so.6"
    "/opt/ZzLogg/plugins/platforms/libqminimal.so"
    "/opt/ZzLogg/bin/qt.conf"
    "ldd"
    [=[grep -F "libQt6Core.so.6 => /opt/ZzLogg/lib/libQt6Core.so.6"]=]
    "QT_DEBUG_PLUGINS=1"
    [=[grep -F "/opt/ZzLogg/plugins/platforms/libqminimal.so" /tmp/zzlogg-smoke.log]=]
    "-platform minimal")
  string(FIND "${linux_job}" "${required}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Linux clean-package validation is missing: ${required}")
  endif()
endforeach()
