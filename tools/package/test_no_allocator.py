#!/usr/bin/env python3
"""Verify bounded C++ components omit libc's allocator and reject hidden allocation."""
from pathlib import Path
import subprocess
import sys
import tempfile

sdk = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_wasm_memory import read_u32_leb
from verify_wasm_core_imports import read_name


def exports(path):
    data, names = path.read_bytes(), set()
    offset = 8
    while offset < len(data):
        kind = data[offset]
        size, offset = read_u32_leb(data, offset+1)
        end = offset + size
        if kind == 7:
            count, cursor = read_u32_leb(data, offset)
            for _ in range(count):
                name, cursor = read_name(data, cursor, end)
                cursor += 1
                _, cursor = read_u32_leb(data, cursor)
                names.add(name)
        offset = end
    return names


with tempfile.TemporaryDirectory(prefix='pxa-no-allocator-') as temporary:
    root = Path(temporary)
    (root/'main.cpp').write_text('''#include <pxa/core.hpp>
#include <cstdlib>
extern "C" int32_t pxa_app_start(const uint8_t*, uint32_t n) {
#if USE_ALLOCATOR
    void* volatile p = std::malloc(n ? n : 8);
    std::free(p);
#endif
    return 0;
}
extern "C" int32_t pxa_app_on_event(const uint8_t*, uint32_t){return 1;}
extern "C" void pxa_app_stop(uint32_t){}
''')
    (root/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.25)
project(allocator_check CXX)
list(APPEND CMAKE_MODULE_PATH "${PXA_CMAKE_MODULE_DIR}")
include(PxaGuest)
if(BOUNDED)
  set(bounded NO_ALLOCATOR)
endif()
pxa_add_component(check CPP ${bounded} COMPONENT_ID main SOURCES main.cpp
                  DEFINITIONS USE_ALLOCATOR=${USE_ALLOCATOR})
''')
    wasi = subprocess.check_output([str(sdk/'tools/package/resolve_wasi_sdk.sh')],text=True).strip()
    log = root/'build.log'
    def build(bounded, allocate):
        command=['cmake','-S',str(root),'-B',str(root/'build'),'-G','Ninja',
                 f'-DCMAKE_TOOLCHAIN_FILE={sdk}/sdk/cmake/pxa-wasi-toolchain.cmake',
                 f'-DWASI_SDK_DIR={wasi}', f'-DPXA_GUEST_SDK_DIR={sdk}/sdk/guest-c',
                 f'-DPXA_CPP_SDK_DIR={sdk}/sdk/guest-cpp', f'-DPXA_CMAKE_MODULE_DIR={sdk}/sdk/cmake',
                 f'-DPXA_ARTIFACT_DIR={root}/artifacts', '-DCMAKE_BUILD_TYPE=Release',
                 f'-DBOUNDED={int(bounded)}', f'-DUSE_ALLOCATOR={int(allocate)}']
        with log.open('w') as output:
            subprocess.run(command,stdout=output,stderr=subprocess.STDOUT,check=True)
            return subprocess.run(['cmake','--build',str(root/'build'),'--parallel','4'],
                                   stdout=output,stderr=subprocess.STDOUT).returncode
    assert build(True,False)==0,log.read_text()
    assert not {'malloc','free'} & exports(root/'artifacts/main.wasm')
    assert build(True,True)!=0,'NO_ALLOCATOR admitted an allocation'
    assert '__wrap_malloc' in log.read_text(),log.read_text()
    assert build(False,True)==0,log.read_text()
    assert {'malloc','free'} <= exports(root/'artifacts/main.wasm')
print('C++ allocator omission, allocation rejection and shared allocator exports PASS')
