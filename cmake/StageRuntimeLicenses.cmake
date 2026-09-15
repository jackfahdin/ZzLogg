if(NOT DEFINED SOURCE_ROOT OR NOT IS_DIRECTORY "${SOURCE_ROOT}")
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED DEPLOY_DIR OR NOT IS_DIRECTORY "${DEPLOY_DIR}")
  message(FATAL_ERROR "DEPLOY_DIR is required")
endif()

function(stage_runtime_notice source_relative destination_relative)
  set(source "${SOURCE_ROOT}/${source_relative}")
  set(destination "${DEPLOY_DIR}/${destination_relative}")
  if(NOT EXISTS "${source}")
    message(FATAL_ERROR "Required release notice is missing: ${source}")
  endif()
  get_filename_component(destination_directory "${destination}" DIRECTORY)
  file(MAKE_DIRECTORY "${destination_directory}")
  file(COPY_FILE "${source}" "${destination}" ONLY_IF_DIFFERENT)
endfunction()

stage_runtime_notice("COPYING" "licenses/ZzLogg/COPYING")
stage_runtime_notice("NOTICE" "licenses/ZzLogg/NOTICE")
stage_runtime_notice("3rdparty/vendor/monocypher/LICENCE.md"
  "licenses/Monocypher/LICENCE.md")
stage_runtime_notice("3rdparty/vendor/ZzPureTools/LICENSE"
  "licenses/ZzPureTools/LICENSE")
stage_runtime_notice("3rdparty/vendor/ZzPureTools/docs/third-party/THIRD_PARTY_NOTICES.md"
  "licenses/ZzPureTools/THIRD_PARTY_NOTICES.md")
stage_runtime_notice("3rdparty/vendor/ZzPureTools/docs/third-party/release-evidence.json"
  "licenses/ZzPureTools/release-evidence.json")
stage_runtime_notice("3rdparty/vendor/ZzPureTools/ZzThirdParty/ZzLog/LICENSE"
  "licenses/ZzPureTools/ZzLog/LICENSE")
stage_runtime_notice("3rdparty/vendor/ZzPureTools/ZzThirdParty/ZzLog/licenses/spdlog/LICENSE.txt"
  "licenses/ZzPureTools/ZzLog/spdlog/LICENSE.txt")
stage_runtime_notice("3rdparty/vendor/ZzPureTools/ZzThirdParty/ZzLog/licenses/fmt/LICENSE.txt"
  "licenses/ZzPureTools/ZzLog/fmt/LICENSE.txt")
stage_runtime_notice("3rdparty/vendor/ZzPureTools/ZzThirdParty/qwindowkit/LICENSE"
  "licenses/ZzPureTools/qwindowkit/LICENSE")
stage_runtime_notice("3rdparty/vendor/ZzPureTools/ZzThirdParty/qwindowkit/qmsetup/LICENSE"
  "licenses/ZzPureTools/qwindowkit/qmsetup/LICENSE")
