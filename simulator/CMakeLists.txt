if(PXSYS_BUILD_HEADLESS_SIMULATOR)
    add_subdirectory(headless)
endif()

if(PXSYS_BUILD_DESKTOP_SIMULATOR)
    add_subdirectory(desktop)
endif()
