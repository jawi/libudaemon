cmake_minimum_required(VERSION 3.5)

project(
    libudaemon
    VERSION 0.5
    LANGUAGES C
)

add_library(udaemon
    src/ud_logging.c
    src/ud_utils.c
    src/udaemon.c
)

target_include_directories(udaemon
    PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(udaemon 
    PRIVATE -Wall -Wstrict-prototypes -Wshadow -Wconversion -Og
)

target_compile_features(udaemon
    PRIVATE c_std_11
)

# Examples

add_executable(test_complete
    example/test_complete.c
)

target_link_libraries(test_complete
    PRIVATE
        udaemon
)

###EOF###