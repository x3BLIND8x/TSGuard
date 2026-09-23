cmake_minimum_required(VERSION 3.20)
file(READ "${CMAKE_SOURCE_DIR}/VERSION" TSGUARD_VERSION)
string(STRIP "${TSGUARD_VERSION}" TSGUARD_VERSION)
project(TSGuard VERSION ${TSGUARD_VERSION} LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(TS3_SDK_DIR "${CMAKE_SOURCE_DIR}/third_party/ts3client-pluginsdk" CACHE PATH "Path to the TeamSpeak 3 Plugin SDK")
if(NOT EXISTS "${TS3_SDK_DIR}/include/ts3_functions.h")
    message(FATAL_ERROR "TeamSpeak Plugin SDK missing. Run scripts/bootstrap-sdk.sh (Linux/macOS/Git Bash) or scripts/bootstrap-sdk.ps1 (Windows), then configure again.")
endif()

add_library(tsguard SHARED src/tsguard.c)
target_include_directories(tsguard PRIVATE "${TS3_SDK_DIR}/include")
target_compile_definitions(tsguard PRIVATE TSG_VERSION="${TSGUARD_VERSION}")

set_target_properties(tsguard PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)

foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_upper)
    set_target_properties(tsguard PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}/out"
        RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}/out"
    )
endforeach()

if(WIN32)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "TSGuard supports the 64-bit TeamSpeak 3 client only on Windows.")
    endif()
    set_target_properties(tsguard PROPERTIES OUTPUT_NAME "tsguard_win64")
    target_compile_definitions(tsguard PRIVATE _CRT_SECURE_NO_WARNINGS)

    if(MSVC)
        # Keep the release DLL self-contained instead of requiring a matching VC runtime install.
        set_property(TARGET tsguard PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(tsguard PRIVATE /W4 /permissive-)
    else()
        target_compile_options(tsguard PRIVATE -Wall -Wextra -Wpedantic)
    endif()
else()
    set_target_properties(tsguard PROPERTIES OUTPUT_NAME "tsguard")
    find_package(Threads REQUIRED)
    target_link_libraries(tsguard PRIVATE Threads::Threads)

    find_package(X11)
    if(X11_FOUND)
        target_compile_definitions(tsguard PRIVATE TSG_HAVE_X11=1)
        target_include_directories(tsguard PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(tsguard PRIVATE ${X11_LIBRARIES})
    endif()

    target_compile_options(tsguard PRIVATE -Wall -Wextra -Wpedantic -fvisibility=hidden)
endif()
