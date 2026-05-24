if(BOREDOS)
    enable_language(C)

    set(BOREDOS_ZLIB_ROOT "${CMAKE_SOURCE_DIR}/../third_party/zlib")
    add_library(boredos_zlib STATIC
        ${BOREDOS_ZLIB_ROOT}/adler32.c
        ${BOREDOS_ZLIB_ROOT}/compress.c
        ${BOREDOS_ZLIB_ROOT}/crc32.c
        ${BOREDOS_ZLIB_ROOT}/deflate.c
        ${BOREDOS_ZLIB_ROOT}/inffast.c
        ${BOREDOS_ZLIB_ROOT}/inflate.c
        ${BOREDOS_ZLIB_ROOT}/inftrees.c
        ${BOREDOS_ZLIB_ROOT}/gzclose.c
        ${BOREDOS_ZLIB_ROOT}/gzlib.c
        ${BOREDOS_ZLIB_ROOT}/gzread.c
        ${BOREDOS_ZLIB_ROOT}/gzwrite.c
        ${BOREDOS_ZLIB_ROOT}/trees.c
        ${BOREDOS_ZLIB_ROOT}/uncompr.c
        ${BOREDOS_ZLIB_ROOT}/zutil.c
    )
    target_include_directories(boredos_zlib PUBLIC ${BOREDOS_ZLIB_ROOT})

    target_sources(openttd PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/boredos_main.cpp
        ${CMAKE_SOURCE_DIR}/../boredos_upstream_bridge.cpp
        ${CMAKE_SOURCE_DIR}/../boredos_platform.cpp
        ${CMAKE_SOURCE_DIR}/../boredos_cxx_runtime.cpp
    )
    target_include_directories(openttd_lib PRIVATE
        ${CMAKE_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/../../..
        ${BOREDOS_ZLIB_ROOT}
    )
    target_include_directories(openttd PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/..
        ${CMAKE_SOURCE_DIR}/../../..
        ${BOREDOS_ZLIB_ROOT}
    )
    target_compile_definitions(openttd_lib PRIVATE BOREDOS WITH_ZLIB)
    target_compile_definitions(openttd PRIVATE BOREDOS WITH_ZLIB)
    target_link_options(openttd PRIVATE -Wl,--allow-multiple-definition)
    target_link_libraries(openttd
        boredos_zlib
        ${BOREDOS_SDK_ROOT}/lib/libboredos.a
        ${BOREDOS_LIBCXX_ROOT}/lib/libc++.a
        ${BOREDOS_LIBCXX_ROOT}/lib/libc++abi.a
        gcc
    )
endif()
