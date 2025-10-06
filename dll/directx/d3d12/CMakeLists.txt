
spec2def(d3d12.dll d3d12.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    main.c)


add_library(d3d12 MODULE
    ${SOURCE}
    d3d12.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d3d12.def)

set_module_type(d3d12 win32dll)
target_link_libraries(d3d12 uuid dxguid ${PSEH_LIB})
add_importlibs(d3d12 dxgi advapi32 gdi32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET d3d12 DESTINATION reactos/system32 FOR all)
