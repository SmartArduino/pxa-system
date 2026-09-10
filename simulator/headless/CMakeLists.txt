add_executable(pxsys_headless_simulator main.c)
target_compile_features(pxsys_headless_simulator PRIVATE c_std_99)
target_link_libraries(pxsys_headless_simulator PRIVATE
    pxsys::standard pxsys::renderer_headless)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pxsys_headless_simulator PRIVATE -Wall -Wextra -Wpedantic)
    if(PXSYS_WARNINGS_AS_ERRORS)
        target_compile_options(pxsys_headless_simulator PRIVATE -Werror)
    endif()
endif()

if(PXSYS_BUILD_TESTS)
    add_test(NAME pxsys_headless_simulator_smoke COMMAND pxsys_headless_simulator)
endif()
