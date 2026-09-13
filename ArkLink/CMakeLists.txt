cmake_minimum_required(VERSION 3.20)
project(ArkLink C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS "src/*.c")
list(FILTER SRC EXCLUDE REGEX "/Main\\.c$")
add_library(arklink STATIC ${SRC})

target_compile_options(arklink PRIVATE
    -Wall
    -Wextra
    -Werror=unused-variable
    -Werror=unused-function
    -Werror=unused-parameter
    -Werror=unused-but-set-variable
    -Werror=implicit-function-declaration
)

target_compile_definitions(arklink PRIVATE $<$<CONFIG:Debug>:ARK_DEBUG>)

target_include_directories(arklink PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(ArkLink src/Main.c)
target_link_libraries(ArkLink PRIVATE arklink)
target_compile_options(ArkLink PRIVATE -Wall -Wextra -Werror)
