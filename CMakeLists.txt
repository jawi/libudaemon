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
        include
    PRIVATE
        src
)

target_compile_options(udaemon 
    PRIVATE -Wall -Wstrict-prototypes -Wshadow -Wconversion -Og
)

target_compile_features(udaemon
    PRIVATE c_std_11
)

# Installation 

include(GNUInstallDirs)
install(TARGETS udaemon 
    EXPORT udaemon-targets
    LIBRARY  DESTINATION lib
    ARCHIVE  DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    udaemonConfigVersion.cmake
    VERSION ${PACKAGE_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_file(
    cmake/udaemonConfig.cmake.in cmake/udaemonConfig.cmake @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/udaemonConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/udaemonConfigVersion.cmake"
        DESTINATION lib/cmake/udaemon
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