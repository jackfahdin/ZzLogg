function(zzlogg_install_legacy_linux_icons source_root)
  foreach(size IN ITEMS 16 32 48)
    install(
      FILES "${source_root}/src/app/images/hicolor/${size}x${size}/ZzLogg.png"
      DESTINATION "share/icons/hicolor/${size}x${size}/apps"
      RENAME klogg.png)
  endforeach()

  install(
    FILES "${source_root}/src/app/images/hicolor/scalable/ZzLogg.svg"
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME klogg.svg)
endfunction()
