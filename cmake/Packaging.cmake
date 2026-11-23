# Release packages (CPack): one archive with the game and its notices.
#   Windows: d2k-faithful-<version>-windows-x64.zip   (cpack -G ZIP -C Release)
#   Linux:   d2k-faithful-<version>-linux-x64.tar.gz  (cpack -G TGZ)
# The source package is `git archive` of HEAD (target `source_package`), so
# it holds exactly the tracked files: no game data, no decompiled listings.

install(TARGETS d2k RUNTIME DESTINATION .)
install(FILES ${PROJECT_SOURCE_DIR}/README.md ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION .)
install(FILES ${sdl3_SOURCE_DIR}/LICENSE.txt DESTINATION . RENAME LICENSE-SDL3.txt)

if(WIN32)
    set(d2k_platform_name windows-x64)
    set(CPACK_GENERATOR ZIP)
else()
    set(d2k_platform_name linux-x64)
    set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_PACKAGE_NAME d2k-faithful)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME d2k-faithful-${PROJECT_VERSION}-${d2k_platform_name})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A faithful reimplementation of Dune 2000 (1998) that runs on your own game data")
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_DIRECTORY ${CMAKE_BINARY_DIR}/packages)
include(CPack)

find_package(Git QUIET)
if(GIT_FOUND)
    add_custom_target(source_package
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/packages
        COMMAND ${GIT_EXECUTABLE} archive --format=zip --prefix=d2k-faithful-${PROJECT_VERSION}-source/
                -o ${CMAKE_BINARY_DIR}/packages/d2k-faithful-${PROJECT_VERSION}-source.zip HEAD
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Source package from git HEAD"
        VERBATIM)
endif()
