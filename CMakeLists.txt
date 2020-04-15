cmake_minimum_required(VERSION 3.5)

project(
    libudaemon
    VERSION 0.8
    LANGUAGES C
)

include(CheckSymbolExists)
check_symbol_exists(FD_CLOEXEC "fcntl.h" HAVE_FD_CLOEXEC)

# generate an include file with the current version information
configure_file(
    cmake/ud_version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/udaemon/ud_version.h @ONLY
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
        src
)

target_compile_options(udaemon 
    PRIVATE -Wall -Wextra -Wstrict-prototypes -Wshadow -Wconversion
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

install(EXPORT udaemon-targets
  FILE udaemonTargets.cmake
  NAMESPACE udaemon::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/udaemon
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    udaemonConfigVersion.cmake
    VERSION ${PACKAGE_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_file(
    cmake/udaemonConfig.cmake.in udaemonConfig.cmake @ONLY
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