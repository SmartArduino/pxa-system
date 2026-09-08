if(NOT COMMAND idf_component_register)
    cmake_minimum_required(VERSION 3.16)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/PxaSystemSources.cmake")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${PXSYS_CORE_SOURCES}
        INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")
else()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        project(pxa_system_core VERSION 0.1.0 LANGUAGES C)
    endif()

    option(PXSYS_BUILD_TESTS "Build pxa_system_core host tests" ON)
    option(PXSYS_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

    add_library(pxa_system_core ${PXSYS_CORE_SOURCES})
    add_library(pxsys::core ALIAS pxa_system_core)
    set_target_properties(pxa_system_core PROPERTIES EXPORT_NAME core)
    target_compile_features(pxa_system_core PUBLIC c_std_99)
    target_include_directories(pxa_system_core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_system_core PRIVATE -Wall -Wextra -Wpedantic)
        if(PXSYS_WARNINGS_AS_ERRORS)
            target_compile_options(pxa_system_core PRIVATE -Werror)
        endif()
    endif()

    if(PXSYS_BUILD_TESTS)
        enable_testing()
        add_executable(pxsys_app_registry_test tests/app_registry_test.c)
        target_link_libraries(pxsys_app_registry_test PRIVATE pxsys::core)
        add_executable(pxsys_app_lifecycle_test tests/app_lifecycle_test.c)
        target_link_libraries(pxsys_app_lifecycle_test PRIVATE pxsys::core)
        add_executable(pxsys_api_compile_test tests/api_compile_test.c)
        target_link_libraries(pxsys_api_compile_test PRIVATE pxsys::core)
        add_executable(pxsys_runtime_test tests/runtime_test.c)
        target_link_libraries(pxsys_runtime_test PRIVATE pxsys::core)
        add_executable(pxsys_intent_test tests/intent_test.c)
        target_link_libraries(pxsys_intent_test PRIVATE pxsys::core)
        add_executable(pxsys_intent_wire_test tests/intent_wire_test.c)
        target_link_libraries(pxsys_intent_wire_test PRIVATE pxsys::core)
        add_executable(pxsys_task_manager_test tests/task_manager_test.c)
        target_link_libraries(pxsys_task_manager_test PRIVATE pxsys::core)
        add_executable(pxsys_role_registry_test tests/role_registry_test.c)
        target_link_libraries(pxsys_role_registry_test PRIVATE pxsys::core)
        add_executable(pxsys_role_host_test tests/role_host_test.c)
        target_link_libraries(pxsys_role_host_test PRIVATE pxsys::core)
        add_executable(pxsys_theme_test tests/theme_test.c)
        target_link_libraries(pxsys_theme_test PRIVATE pxsys::core)
        add_executable(pxsys_renderer_host_test tests/renderer_host_test.c)
        target_link_libraries(pxsys_renderer_host_test PRIVATE pxsys::core)
        add_executable(pxsys_service_registry_test tests/service_registry_test.c)
        target_link_libraries(pxsys_service_registry_test PRIVATE pxsys::core)
        add_executable(pxsys_event_broker_test tests/event_broker_test.c)
        target_link_libraries(pxsys_event_broker_test PRIVATE pxsys::core)
        add_executable(pxsys_display_test tests/display_test.c)
        target_link_libraries(pxsys_display_test PRIVATE pxsys::core)
        add_executable(pxsys_system_status_test tests/system_status_test.c)
        target_link_libraries(pxsys_system_status_test PRIVATE pxsys::core)
        add_executable(pxsys_toast_test tests/toast_test.c)
        target_link_libraries(pxsys_toast_test PRIVATE pxsys::core)
        add_executable(pxsys_window_test tests/window_test.c)
        target_link_libraries(pxsys_window_test PRIVATE pxsys::core)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            foreach(pxsys_test pxsys_app_registry_test pxsys_app_lifecycle_test
                                pxsys_api_compile_test pxsys_runtime_test pxsys_intent_test
                                pxsys_intent_wire_test pxsys_task_manager_test
                                pxsys_role_registry_test pxsys_role_host_test pxsys_theme_test
                                pxsys_renderer_host_test
                                pxsys_service_registry_test pxsys_event_broker_test
                                pxsys_display_test pxsys_system_status_test pxsys_toast_test
                                pxsys_window_test)
                target_compile_options(${pxsys_test} PRIVATE
                    -Wall -Wextra -Wpedantic)
            endforeach()
            if(PXSYS_WARNINGS_AS_ERRORS)
                foreach(pxsys_test pxsys_app_registry_test pxsys_app_lifecycle_test
                                    pxsys_api_compile_test pxsys_runtime_test pxsys_intent_test
                                    pxsys_intent_wire_test pxsys_task_manager_test
                                    pxsys_role_registry_test pxsys_role_host_test pxsys_theme_test
                                    pxsys_renderer_host_test
                                    pxsys_service_registry_test pxsys_event_broker_test
                                    pxsys_display_test pxsys_system_status_test pxsys_toast_test
                                    pxsys_window_test)
                    target_compile_options(${pxsys_test} PRIVATE -Werror)
                endforeach()
            endif()
        endif()
        add_test(NAME pxsys_app_registry_test COMMAND pxsys_app_registry_test)
        add_test(NAME pxsys_app_lifecycle_test COMMAND pxsys_app_lifecycle_test)
        add_test(NAME pxsys_api_compile_test COMMAND pxsys_api_compile_test)
        add_test(NAME pxsys_runtime_test COMMAND pxsys_runtime_test)
        add_test(NAME pxsys_intent_test COMMAND pxsys_intent_test)
        add_test(NAME pxsys_intent_wire_test COMMAND pxsys_intent_wire_test)
        add_test(NAME pxsys_task_manager_test COMMAND pxsys_task_manager_test)
        add_test(NAME pxsys_role_registry_test COMMAND pxsys_role_registry_test)
        add_test(NAME pxsys_role_host_test COMMAND pxsys_role_host_test)
        add_test(NAME pxsys_theme_test COMMAND pxsys_theme_test)
        add_test(NAME pxsys_renderer_host_test COMMAND pxsys_renderer_host_test)
        add_test(NAME pxsys_service_registry_test COMMAND pxsys_service_registry_test)
        add_test(NAME pxsys_event_broker_test COMMAND pxsys_event_broker_test)
        add_test(NAME pxsys_display_test COMMAND pxsys_display_test)
        add_test(NAME pxsys_system_status_test COMMAND pxsys_system_status_test)
        add_test(NAME pxsys_toast_test COMMAND pxsys_toast_test)
        add_test(NAME pxsys_window_test COMMAND pxsys_window_test)
    endif()
endif()
