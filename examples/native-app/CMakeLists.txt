cmake_minimum_required(VERSION 3.16)
project(pxsys_native_example LANGUAGES C)

find_package(pxsys 0.1 REQUIRED)

add_executable(pxsys_native_example main.c)
target_compile_features(pxsys_native_example PRIVATE c_std_99)
target_link_libraries(pxsys_native_example PRIVATE pxsys::core)
