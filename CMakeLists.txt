cmake_minimum_required(VERSION 3.16)
project(BlackboxFX C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wshadow)
    # the engine is what ports to the device; keep its arithmetic explicit
    set(BFX_STRICT -Wconversion -Wno-sign-conversion)
endif()

# The engine: no allocation, no I/O, only <math.h> and <string.h>.
add_library(bfx STATIC
    src/engine.c
    src/character.c
    src/movement.c
    src/cloud.c
    src/tape.c
    src/space.c
    src/looper.c)
target_include_directories(bfx PUBLIC include)
target_compile_options(bfx PRIVATE ${BFX_STRICT})
if(NOT MSVC)
    target_link_libraries(bfx PUBLIC m)
endif()

add_executable(bfx_cli host/bfx_cli.c host/wav.c)
set_target_properties(bfx_cli PROPERTIES OUTPUT_NAME bfx)
target_link_libraries(bfx_cli PRIVATE bfx)

enable_testing()
add_executable(test_engine tests/test_engine.c)
target_link_libraries(test_engine PRIVATE bfx)
add_test(NAME engine COMMAND test_engine)
