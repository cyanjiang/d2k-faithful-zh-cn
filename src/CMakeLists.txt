# Layers (see CLAUDE.md §5). Each is a static library; sim/ must never
# depend on render/audio/video/ui/platform so it stays deterministic.

add_library(d2k_formats STATIC
    formats/aud.cpp
    formats/bmp.cpp
    formats/byte_reader.cpp
    formats/file_io.cpp
    formats/font.cpp
    formats/palette.cpp
    formats/pe_image.cpp
    formats/ima_adpcm.cpp
    formats/inflate.cpp
    formats/installshield3.cpp
    formats/iso9660.cpp
    formats/lcw.cpp
    formats/map.cpp
    formats/r8.cpp
    formats/sound_rs.cpp
    formats/string_table.cpp
    formats/templates.cpp
    formats/tga.cpp
    formats/vqa.cpp
    formats/zip.cpp)
target_include_directories(d2k_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Optional localisation: pure standard library, no SDL and no game data,
# so it can be built and unit-tested on its own.
add_library(d2k_i18n STATIC i18n/i18n.cpp i18n/cjk_font.cpp i18n/translation.cpp i18n/utf8.cpp)
target_include_directories(d2k_i18n PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The 1.06 manifest (docs/first-run-setup.md): the files our engine reads,
# with size and SHA-256, generated from docs/inventory-1.06.csv. Hashes only.
set(D2K_INVENTORY ${PROJECT_SOURCE_DIR}/docs/inventory-1.06.csv)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${D2K_INVENTORY})
file(STRINGS ${D2K_INVENTORY} d2k_inventory_lines)
set(d2k_manifest "")
foreach(line IN LISTS d2k_inventory_lines)
    if(line MATCHES "^([^,]+),([0-9]+),([0-9a-f]+),")
        set(p "${CMAKE_MATCH_1}")
        set(size "${CMAKE_MATCH_2}")
        set(hash "${CMAKE_MATCH_3}")
        if(p STREQUAL "DUNE2000.DAT" OR p MATCHES "^(Data|MISSIONS|MOVIES|MUSIC)/") # (resets CMAKE_MATCH_*)
            string(APPEND d2k_manifest "    {\"${p}\", ${size}u, \"${hash}\"},\n")
        endif()
    endif()
endforeach()
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/manifest_106.inc
     CONTENT "// Generated from docs/inventory-1.06.csv; do not edit.\n${d2k_manifest}")

add_library(d2k_platform STATIC
    platform/game_data.cpp
    platform/manifest.cpp
    platform/setup_sources.cpp
    platform/sha256.cpp
    platform/settings.cpp)
target_link_libraries(d2k_platform PUBLIC d2k_formats)
target_include_directories(d2k_platform PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_library(d2k_sim STATIC
    sim/exe_tables.cpp
    sim/game_map.cpp
    sim/game_setup.cpp
    sim/objects.cpp
    sim/aircraft.cpp
    sim/combat.cpp
    sim/economy.cpp
    sim/harvest.cpp
    sim/mission.cpp
    sim/network.cpp
    sim/placement.cpp
    sim/orders.cpp
    sim/groups.cpp
    sim/ai.cpp
    sim/ai_groups.cpp
    sim/ai_rebuild.cpp
    sim/save_state.cpp
    sim/pathfind.cpp
    sim/blooms.cpp
    sim/capture.cpp
    sim/starport.cpp
    sim/ferry.cpp
    sim/shroud.cpp
    sim/tick_scheduler.cpp
    sim/world.cpp
    sim/skirmish.cpp
    sim/worm.cpp)
target_link_libraries(d2k_sim PUBLIC d2k_formats)

# Network games: the lockstep (no sockets) and, later, the transport.
add_library(d2k_net STATIC
    net/lobby.cpp
    net/lockstep.cpp
    net/transport.cpp
    net/udp.cpp)
target_link_libraries(d2k_net PUBLIC d2k_sim)
target_compile_definitions(d2k_net PRIVATE D2K_VERSION="${PROJECT_VERSION}")
# The build fingerprint in the network version check, refreshed on every build.
set(D2K_FINGERPRINT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_target(d2k_build_fingerprint
    COMMAND "${CMAKE_COMMAND}" -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DOUT=${D2K_FINGERPRINT_DIR}/build_fingerprint.hpp
            "-DCOMPILER=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
            -P "${PROJECT_SOURCE_DIR}/cmake/BuildFingerprint.cmake"
    BYPRODUCTS "${D2K_FINGERPRINT_DIR}/build_fingerprint.hpp"
    COMMENT "Build fingerprint"
    VERBATIM)
add_dependencies(d2k_net d2k_build_fingerprint)
target_include_directories(d2k_net PRIVATE "${D2K_FINGERPRINT_DIR}")
if(WIN32)
    target_link_libraries(d2k_net PUBLIC ws2_32)
endif()

add_library(d2k_campaign STATIC
    campaign/campaign_table.cpp
    campaign/regions.cpp
    campaign/score.cpp)
target_link_libraries(d2k_campaign PUBLIC d2k_formats)

add_library(d2k_ui STATIC
    ui/assets.cpp
    ui/credits.cpp
    ui/globe.cpp
    ui/menu_hooks.cpp
    ui/animation.cpp
    ui/render.cpp
    ui/screen.cpp
    ui/surface.cpp
    ui/transition.cpp)
target_link_libraries(d2k_ui PUBLIC d2k_formats d2k_platform d2k_campaign d2k_i18n)

add_library(d2k_render STATIC
    render/display.cpp
    render/objects.cpp
    render/sprites.cpp
    render/terrain.cpp)
target_include_directories(d2k_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(d2k_render PUBLIC d2k_ui d2k_sim SDL3::SDL3)

add_library(d2k_video STATIC
    video/movie_player.cpp
    video/enhanced_video.cpp)
target_link_libraries(d2k_video PUBLIC d2k_formats d2k_render SDL3::SDL3)
if(WIN32)
    # Upscaled movies (enhanced_movies option): Windows Media Foundation's
    # H.264 decoder, part of Windows.
    target_link_libraries(d2k_video PRIVATE mfplat mfreadwrite mfuuid ole32)
endif()

add_library(d2k_audio STATIC
    audio/music_player.cpp
    audio/sample_player.cpp)
target_link_libraries(d2k_audio PUBLIC d2k_formats d2k_platform SDL3::SDL3)

# The menu runner in a library of its own, so the tests can drive it.
add_library(d2k_menus STATIC app/menu_runner.cpp app/menu_lobby.cpp app/user_dirs.cpp app/save_games.cpp app/display_mode.cpp)
target_link_libraries(d2k_menus PUBLIC d2k_platform d2k_ui d2k_render d2k_video d2k_audio d2k_net SDL3::SDL3)

add_executable(d2k app/main.cpp app/game_screen.cpp app/sidebar.cpp app/setup_screen.cpp app/display_setup_screen.cpp)
target_link_libraries(d2k PRIVATE d2k_menus d2k_platform d2k_ui d2k_render d2k_video d2k_audio d2k_net SDL3::SDL3)
if(WIN32)
    set_target_properties(d2k PROPERTIES WIN32_EXECUTABLE ON)
endif()

# Optional localisation packs live outside the hash-checked 1.06 game data.
# Copy them next to the executable so a build straight out of the source tree
# finds i18n/<lang>/ without any extra setup (the runtime also walks up a few
# parent directories for developer builds).
if(EXISTS "${CMAKE_SOURCE_DIR}/i18n")
    add_custom_command(TARGET d2k POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/i18n" "$<TARGET_FILE_DIR:d2k>/i18n"
        COMMENT "Copying i18n language packs next to d2k.exe"
        VERBATIM)
endif()

# Double-click entry point for the graphical display setup wizard.
if(EXISTS "${CMAKE_SOURCE_DIR}/显示设置.bat")
    add_custom_command(TARGET d2k POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/显示设置.bat" "$<TARGET_FILE_DIR:d2k>/显示设置.bat"
        COMMENT "Copying display setup launcher next to d2k.exe"
        VERBATIM)
endif()
