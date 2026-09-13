cmake_minimum_required(VERSION 3.20)
project(KrtC C)

# Share the C source manifest with Zig and the regression harnesses.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/build.zig")
file(STRINGS "build.zig" SOURCE_LINES REGEX "\"[^\"]+\\.c\"")
set(KRTC_SOURCES)
foreach(LINE IN LISTS SOURCE_LINES)
    string(REGEX MATCH "\"([^\"]+\\.c)\"" MATCHED "${LINE}")
    list(APPEND KRTC_SOURCES "${CMAKE_MATCH_1}")
endforeach()

if(NOT TARGET arklink)
    add_subdirectory("../ArkLink" "ArkLink")
endif()
find_package(Threads REQUIRED)
add_executable(KrtC ${KRTC_SOURCES})
target_compile_features(KrtC PRIVATE c_std_11)
set_target_properties(KrtC PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
target_include_directories(KrtC PRIVATE src src/Core src/Tools src/Bytecode Shared stub_include vm)
target_compile_options(KrtC PRIVATE -Wall -Wextra -Wformat=2 -Wshadow
    -Werror=unused-variable -Werror=unused-function -Werror=unused-parameter
    -Werror=unused-but-set-variable -Werror=implicit-function-declaration)
target_link_libraries(KrtC PRIVATE arklink Threads::Threads m)
