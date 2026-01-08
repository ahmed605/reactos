
remove_definitions(-D_WIN32_WINNT=0x502)
add_definitions(-D_WIN32_WINNT=0x600)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

spec2def(dxgkrnl.sys dxgkrnl.spec ADD_IMPORTLIB)

list(APPEND SOURCE
     entry.cpp
     device/dxgkrnlinterface.cpp
     device/postdisplay_vbe.c
     miniport/registry.cpp
     miniport/rxgkdispatch.cpp
     miniport/rxgkport.cpp
     videoss/vidpininterface.cpp
     videoss/monitorinterface.cpp
     kmt/win32k_callbacks.cpp
     kmt/d3dkmt_test.cpp
     kmt/allocation.cpp
     kmt/adapter.cpp
     kmt/handles.cpp
     kmt/context.cpp
     kmt/device.cpp
     kmt/devicestate.cpp
     kmt/escape.cpp
     )

add_library(dxgkrnl MODULE
    ${SOURCE}
    dxgkrnl.rc
    ${CMAKE_CURRENT_BINARY_DIR}/dxgkrnl_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/dxgkrnl.def)

if(USE_CLANG_CL)
    target_compile_options(dxgkrnl PRIVATE "-Wno-missing-braces")
endif()

add_pch(dxgkrnl rxgkrnl.h "${PCH_SKIP_SOURCE}")
set_module_type(dxgkrnl kernelmodedriver ENTRYPOINT DriverEntry)
add_importlibs(dxgkrnl ntoskrnl hal)
add_cd_file(TARGET dxgkrnl DESTINATION reactos/system32/drivers FOR all)
