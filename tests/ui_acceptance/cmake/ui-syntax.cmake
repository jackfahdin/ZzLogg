add_executable(zzlogg_syntax_highlight_test ui/syntaxhighlighttest.cpp)
target_link_libraries(zzlogg_syntax_highlight_test PRIVATE klogg_ui Qt6::Test)
set_target_properties(zzlogg_syntax_highlight_test PROPERTIES AUTOMOC ON)
add_test(NAME zzlogg_ui.syntax_highlight COMMAND zzlogg_syntax_highlight_test)
if(WIN32)
  set_property(TEST zzlogg_ui.syntax_highlight PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:tbb>")
endif()
