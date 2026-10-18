# Keep the portable runtime policy identical on desktop and embedded Hosts.
include("${CMAKE_CURRENT_LIST_DIR}/PxaWamrLoader.cmake")

function(pxa_configure_wamr_runtime target wamr_root)
    # Without this upstream mode, enabling lib-pthread silently partitions a
    # module's declared auxiliary stack among the main thread and four unused
    # child slots. PXA rejects thread imports, so ordinary Apps must receive the
    # full declared stack. Any future child stacks are allocated only on spawn.
    # Define this on the whole WAMR target: WASMCluster's layout also changes.
    target_compile_definitions(${target} PRIVATE
        WASM_ENABLE_HEAP_AUX_STACK_ALLOCATION=1)
    pxa_configure_wamr_loader(${target} "${wamr_root}")
endfunction()
