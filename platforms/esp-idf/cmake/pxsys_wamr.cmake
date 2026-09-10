# Project-owned compatibility configuration for the pinned upstream WAMR.
# Fail loudly when upstream changes a patched site instead of silently building
# against an incompatible runtime implementation.

get_filename_component(PXSYS_WAMR_ESP_IDF_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(PXSYS_WAMR_ROOT "${PXSYS_WAMR_ESP_IDF_DIR}/../.." ABSOLUTE)

function(pxsys_replace_wamr_source wamr_component original_source replacement_source)
    get_property(wamr_component_sources TARGET ${wamr_component} PROPERTY SOURCES)
    list(FIND wamr_component_sources "${original_source}" original_source_index)
    if(original_source_index EQUAL -1)
        message(FATAL_ERROR
            "WAMR target does not contain source to replace: ${original_source}")
    endif()

    list(REMOVE_AT wamr_component_sources ${original_source_index})
    set_property(TARGET ${wamr_component} PROPERTY SOURCES
        "${wamr_component_sources}")
    target_sources(${wamr_component} PRIVATE "${replacement_source}")
endfunction()

function(pxsys_configure_wamr_esp_idf wamr_component)
    if(NOT TARGET ${wamr_component})
        message(FATAL_ERROR "WAMR component target is missing: ${wamr_component}")
    endif()

    set(wamr_root "${PXSYS_WAMR_ROOT}/wamr")
    set(wamr_memmap_source
        "${wamr_root}/core/shared/platform/esp-idf/espidf_memmap.c")
    set(wamr_file_source
        "${wamr_root}/core/shared/platform/esp-idf/espidf_file.c")
    set(wamr_platform_header
        "${wamr_root}/core/shared/platform/esp-idf/platform_internal.h")
    set(wamr_aot_loader_source "${wamr_root}/core/iwasm/aot/aot_loader.c")
    set(wamr_xtensa_reloc_source
        "${wamr_root}/core/iwasm/aot/arch/aot_reloc_xtensa.c")
    foreach(wamr_source IN ITEMS ${wamr_memmap_source} ${wamr_file_source}
            ${wamr_platform_header} ${wamr_aot_loader_source}
            ${wamr_xtensa_reloc_source})
        if(NOT EXISTS "${wamr_source}")
            message(FATAL_ERROR "Unsupported WAMR source layout: ${wamr_source}")
        endif()
    endforeach()

    pxsys_replace_wamr_source(${wamr_component} ${wamr_memmap_source}
        "${PXSYS_WAMR_ESP_IDF_DIR}/src/wamr_espidf_memmap.c")

    # WAMR main abstracts the POSIX types used by libc-wasi. ESP-IDF provides
    # those POSIX APIs, but its upstream platform header uses scalar stubs.
    # Overlay a corrected header without modifying the WAMR submodule.
    file(READ ${wamr_platform_header} wamr_platform_header_content)
    set(wamr_platform_includes_from "#include <dirent.h>")
    set(wamr_platform_includes_to "#include <dirent.h>\n#include <sys/ioctl.h>\n#include <sys/poll.h>\n#include <time.h>")
    string(FIND "${wamr_platform_header_content}" "${wamr_platform_includes_from}"
        wamr_platform_includes_offset)
    if(wamr_platform_includes_offset EQUAL -1)
        message(FATAL_ERROR "Unsupported WAMR ESP platform header includes")
    endif()
    string(REPLACE "${wamr_platform_includes_from}" "${wamr_platform_includes_to}"
        wamr_platform_header_content "${wamr_platform_header_content}")
    set(wamr_platform_types_from "typedef int os_poll_file_handle;\ntypedef unsigned int os_nfds_t;\ntypedef int os_timespec;")
    set(wamr_platform_types_to "typedef struct pollfd os_poll_file_handle;\ntypedef nfds_t os_nfds_t;\ntypedef struct timespec os_timespec;")
    string(FIND "${wamr_platform_header_content}" "${wamr_platform_types_from}"
        wamr_platform_types_offset)
    if(wamr_platform_types_offset EQUAL -1)
        message(FATAL_ERROR "Unsupported WAMR ESP platform POSIX type definitions")
    endif()
    string(REPLACE "${wamr_platform_types_from}" "${wamr_platform_types_to}"
        wamr_platform_header_content "${wamr_platform_header_content}")
    set(wamr_platform_include_dir "${CMAKE_CURRENT_BINARY_DIR}/pxsys_wamr_include")
    file(MAKE_DIRECTORY ${wamr_platform_include_dir})
    file(WRITE "${wamr_platform_include_dir}/platform_internal.h"
        "${wamr_platform_header_content}")
    target_include_directories(${wamr_component} BEFORE PRIVATE
        ${wamr_platform_include_dir})

    # ESP-IDF nullfs returns ENOSYS for F_GETFL.  A host-owned character
    # device remains usable by WASI, so handle only that narrow fallback.
    file(READ ${wamr_file_source} wamr_file_content)
    set(wamr_getfl_from "    int ret = fcntl(handle, F_GETFL, 0);\n\n    if (ret < 0)\n        return convert_errno(errno);")
    set(wamr_getfl_to "    int ret = fcntl(handle, F_GETFL, 0);\n\n    if (ret < 0) {\n        int getfl_errno = errno;\n        struct stat stat_buf;\n\n        if (getfl_errno == ENOSYS && fstat(handle, &stat_buf) == 0\n            && S_ISCHR(stat_buf.st_mode)) {\n            *access_mode = WASI_LIBC_ACCESS_MODE_READ_WRITE;\n            return __WASI_ESUCCESS;\n        }\n        return convert_errno(getfl_errno);\n    }")
    string(FIND "${wamr_file_content}" "${wamr_getfl_from}" wamr_getfl_offset)
    if(wamr_getfl_offset EQUAL -1)
        message(FATAL_ERROR "Unsupported WAMR espidf_file.c F_GETFL implementation")
    endif()
    string(REPLACE "${wamr_getfl_from}" "${wamr_getfl_to}"
        wamr_file_content "${wamr_file_content}")
    set(wamr_ioctl_from "int\nos_ioctl(os_file_handle handle, int request, ...)\n{\n    return BHT_ERROR;\n}\n\nint\nos_poll(os_poll_file_handle *fds, os_nfds_t nfs, int timeout)\n{\n    return BHT_ERROR;\n}")
    set(wamr_ioctl_to "int\nos_ioctl(os_file_handle handle, int request, ...)\n{\n    va_list arguments;\n    void *argument;\n\n    va_start(arguments, request);\n    argument = va_arg(arguments, void *);\n    va_end(arguments);\n    return ioctl(handle, request, argument);\n}\n\nint\nos_poll(os_poll_file_handle *fds, os_nfds_t nfs, int timeout)\n{\n    return poll(fds, nfs, timeout);\n}\n\nbool\nos_compare_file_handle(os_file_handle handle1, os_file_handle handle2)\n{\n    return handle1 == handle2;\n}")
    string(FIND "${wamr_file_content}" "${wamr_ioctl_from}" wamr_ioctl_offset)
    if(wamr_ioctl_offset EQUAL -1)
        message(FATAL_ERROR "Unsupported WAMR espidf_file.c WASI poll implementation")
    endif()
    string(REPLACE "${wamr_ioctl_from}" "${wamr_ioctl_to}"
        wamr_file_content "${wamr_file_content}")
    set(wamr_file_copy "${CMAKE_CURRENT_BINARY_DIR}/pxsys_wamr_espidf_file.c")
    file(WRITE ${wamr_file_copy} "${wamr_file_content}")
    pxsys_replace_wamr_source(${wamr_component} ${wamr_file_source}
        ${wamr_file_copy})

    # Upstream allocates AOT text through the instruction bus on ESP32-S3.
    # Its trailing zero fill must use the data-bus alias of that mapping.
    file(READ ${wamr_aot_loader_source} wamr_aot_loader_content)
    set(wamr_zero_from "                        memset(aot_text + (uint32)section_size, 0,\n                               (uint32)total_size - section_size);")
    set(wamr_zero_to "#if (WASM_MEM_DUAL_BUS_MIRROR != 0)\n                        memset(os_get_dbus_mirror(aot_text)\n                                   + (uint32)section_size, 0,\n                               (uint32)total_size - section_size);\n#else\n                        memset(aot_text + (uint32)section_size, 0,\n                               (uint32)total_size - section_size);\n#endif")
    string(FIND "${wamr_aot_loader_content}" "${wamr_zero_from}" wamr_zero_offset)
    if(wamr_zero_offset EQUAL -1)
        message(FATAL_ERROR "Unsupported WAMR aot_loader.c zero-fill implementation")
    endif()
    string(REPLACE "${wamr_zero_from}" "${wamr_zero_to}"
        wamr_aot_loader_content "${wamr_aot_loader_content}")
    set(wamr_aot_loader_copy "${CMAKE_CURRENT_BINARY_DIR}/pxsys_wamr_aot_loader.c")
    file(WRITE ${wamr_aot_loader_copy} "${wamr_aot_loader_content}")
    pxsys_replace_wamr_source(${wamr_component} ${wamr_aot_loader_source}
        ${wamr_aot_loader_copy})

    # LLVM 22 lowers 32-bit division and remainder to these libgcc helpers for
    # Xtensa AOT. WAMR main's Xtensa relocation map omits them even though ESP
    # provides them. Referencing them here also ensures the linker retains them.
    file(READ ${wamr_xtensa_reloc_source} wamr_xtensa_reloc_content)
    set(wamr_divsi3_declaration_from "void __modsi3(void);\n\nvoid __divdi3(void);")
    set(wamr_divsi3_declaration_to
        "void __modsi3(void);\nvoid __divsi3(void);\n\nvoid __divdi3(void);")
    string(FIND "${wamr_xtensa_reloc_content}" "${wamr_divsi3_declaration_from}"
        wamr_divsi3_declaration_offset)
    if(wamr_divsi3_declaration_offset EQUAL -1)
        message(FATAL_ERROR "Unsupported WAMR Xtensa signed division declarations")
    endif()
    string(REPLACE "${wamr_divsi3_declaration_from}" "${wamr_divsi3_declaration_to}"
        wamr_xtensa_reloc_content "${wamr_xtensa_reloc_content}")
    set(wamr_udivsi3_declaration_from "void __udivdi3(void);")
    set(wamr_udivsi3_declaration_to
        "void __udivsi3(void);\nvoid __udivdi3(void);")
    string(FIND "${wamr_xtensa_reloc_content}" "${wamr_udivsi3_declaration_from}"
        wamr_udivsi3_declaration_offset)
    if(wamr_udivsi3_declaration_offset EQUAL -1)
        message(FATAL_ERROR "Unsupported WAMR Xtensa unsigned division declarations")
    endif()
    string(REPLACE "${wamr_udivsi3_declaration_from}" "${wamr_udivsi3_declaration_to}"
        wamr_xtensa_reloc_content "${wamr_xtensa_reloc_content}")
    set(wamr_umodsi3_declaration_from "void __umoddi3(void);")
    set(wamr_umodsi3_declaration_to
        "void __umodsi3(void);\nvoid __umoddi3(void);")
    string(FIND "${wamr_xtensa_reloc_content}" "${wamr_umodsi3_declaration_from}"
        wamr_umodsi3_declaration_offset)
    if(wamr_umodsi3_declaration_offset EQUAL -1)
        message(FATAL_ERROR "Unsupported WAMR Xtensa unsigned remainder declarations")
    endif()
    string(REPLACE "${wamr_umodsi3_declaration_from}" "${wamr_umodsi3_declaration_to}"
        wamr_xtensa_reloc_content "${wamr_xtensa_reloc_content}")
    set(wamr_divsi3_symbol_from "    REG_SYM(__modsi3),\n    REG_SYM(__divdi3),")
    set(wamr_divsi3_symbol_to
        "    REG_SYM(__modsi3),\n    REG_SYM(__divsi3),\n    REG_SYM(__divdi3),")
    string(FIND "${wamr_xtensa_reloc_content}" "${wamr_divsi3_symbol_from}"
        wamr_divsi3_symbol_offset)
    if(wamr_divsi3_symbol_offset EQUAL -1)
        message(FATAL_ERROR "Unsupported WAMR Xtensa signed division symbol map")
    endif()
    string(REPLACE "${wamr_divsi3_symbol_from}" "${wamr_divsi3_symbol_to}"
        wamr_xtensa_reloc_content "${wamr_xtensa_reloc_content}")
    set(wamr_udivsi3_symbol_from "    REG_SYM(__udivdi3),")
    set(wamr_udivsi3_symbol_to "    REG_SYM(__udivsi3),\n    REG_SYM(__udivdi3),")
    string(FIND "${wamr_xtensa_reloc_content}" "${wamr_udivsi3_symbol_from}"
        wamr_udivsi3_symbol_offset)
    if(wamr_udivsi3_symbol_offset EQUAL -1)
        message(FATAL_ERROR "Unsupported WAMR Xtensa unsigned division symbol map")
    endif()
    string(REPLACE "${wamr_udivsi3_symbol_from}" "${wamr_udivsi3_symbol_to}"
        wamr_xtensa_reloc_content "${wamr_xtensa_reloc_content}")
    set(wamr_umodsi3_symbol_from "    REG_SYM(__umoddi3),")
    set(wamr_umodsi3_symbol_to
        "    REG_SYM(__umodsi3),\n    REG_SYM(__umoddi3),")
    string(FIND "${wamr_xtensa_reloc_content}" "${wamr_umodsi3_symbol_from}"
        wamr_umodsi3_symbol_offset)
    if(wamr_umodsi3_symbol_offset EQUAL -1)
        message(FATAL_ERROR "Unsupported WAMR Xtensa unsigned remainder symbol map")
    endif()
    string(REPLACE "${wamr_umodsi3_symbol_from}" "${wamr_umodsi3_symbol_to}"
        wamr_xtensa_reloc_content "${wamr_xtensa_reloc_content}")
    set(wamr_xtensa_reloc_copy
        "${CMAKE_CURRENT_BINARY_DIR}/pxsys_wamr_aot_reloc_xtensa.c")
    file(WRITE ${wamr_xtensa_reloc_copy} "${wamr_xtensa_reloc_content}")
    pxsys_replace_wamr_source(${wamr_component} ${wamr_xtensa_reloc_source}
        ${wamr_xtensa_reloc_copy})

    target_link_libraries(${wamr_component} PRIVATE __idf_esp_mm)
endfunction()
