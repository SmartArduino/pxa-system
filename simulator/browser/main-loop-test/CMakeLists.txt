cmake_minimum_required(VERSION 3.20)
project(pxa_browser_mainloop_test C)
find_package(Pxa CONFIG REQUIRED)
pxa_add_component(main COMPONENT_ID main NO_ALLOCATOR SOURCES main.c)
