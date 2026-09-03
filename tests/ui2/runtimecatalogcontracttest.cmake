if(NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(required_crawler_sources
  "Match case"
  "Use regex"
  "Inverse match"
  "Enable regular expression logical combining"
  "Auto-refresh"
  "Edit search history"
  "Keep these results and show subsequent results in a new window"
  "%1 matches found"
  "%1 match found"
  "File truncated on disk")

foreach(language IN ITEMS en zh_CN zh_TW)
  file(STRINGS "${SOURCE_ROOT}/src/app/i18n/${language}.ts" catalog_lines ENCODING UTF-8)
  set(in_crawler_context FALSE)
  set(pending_source "")
  set(finished_sources "")

  foreach(line IN LISTS catalog_lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "<name>CrawlerWidget</name>")
      set(in_crawler_context TRUE)
    elseif(in_crawler_context AND line STREQUAL "</context>")
      break()
    elseif(in_crawler_context)
      foreach(source IN LISTS required_crawler_sources)
        if(line STREQUAL "<source>${source}</source>")
          set(pending_source "${source}")
          break()
        endif()
      endforeach()

      if(NOT pending_source STREQUAL "" AND line MATCHES "^<translation")
        if(line MATCHES "type=\"unfinished\"")
          message(FATAL_ERROR
            "${language}: unfinished CrawlerWidget translation: ${pending_source}")
        endif()
        string(REGEX REPLACE "^<translation[^>]*>|</translation>$" ""
          translation_text "${line}")
        string(STRIP "${translation_text}" translation_text)
        if(translation_text STREQUAL "")
          message(FATAL_ERROR
            "${language}: empty CrawlerWidget translation: ${pending_source}")
        endif()
        list(APPEND finished_sources "${pending_source}")
        set(pending_source "")
      endif()
    endif()
  endforeach()

  foreach(source IN LISTS required_crawler_sources)
    list(FIND finished_sources "${source}" source_index)
    if(source_index EQUAL -1)
      message(FATAL_ERROR
        "${language}: missing CrawlerWidget translation contract entry: ${source}")
    endif()
  endforeach()
endforeach()
