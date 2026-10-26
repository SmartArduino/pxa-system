cmake_minimum_required(VERSION 3.20)
project(pxa_browser_services C)
find_package(Pxa CONFIG REQUIRED)
pxa_add_component(main COMPONENT_ID main SOURCES main.c)
