add_executable(d2k_tests
    main.cpp
    audio/test_channels.cpp
    campaign/test_front_end.cpp
    audio/test_sample_cache.cpp
    formats/test_archives.cpp
    formats/test_audio.cpp
    formats/test_byte_reader.cpp
    formats/test_r8.cpp
    formats/test_string_table.cpp
    formats/test_vqa.cpp
    i18n/test_i18n.cpp
    net/test_lobby.cpp
    net/test_mp_rules.cpp
    net/test_transport.cpp
    platform/test_game_data.cpp
    platform/test_setup.cpp
    sim/test_ai.cpp
    sim/test_combat.cpp
    sim/test_economy_map.cpp
    sim/test_leftover_sim.cpp
    sim/test_ingame_input.cpp
    sim/test_map.cpp
    sim/test_network.cpp
    sim/test_placement.cpp
    sim/test_sell_repair_capture.cpp
    sim/test_start_view.cpp
    sim/test_aircraft_specials.cpp
    sim/test_pool.cpp
    sim/test_world.cpp
    sim/test_worm_harvest.cpp
    ui/test_formats_drawing.cpp
    ui/test_ingame_menus.cpp
    ui/test_ingame_recap.cpp
    ui/test_menu_input.cpp
    ui/test_score_mp.cpp
    ui/test_screen_parity.cpp
    ui/test_ui.cpp
    video/test_enhanced.cpp)
target_link_libraries(d2k_tests PRIVATE d2k_formats d2k_platform d2k_campaign d2k_sim d2k_net d2k_ui d2k_menus doctest::doctest)
# Golden tests against real data look here; they skip if it is absent.
# Settable, so a second checkout (a git worktree) can use the main one's data.
set(D2K_GAMEDATA_DIR "${PROJECT_SOURCE_DIR}/gamedata/1.06" CACHE PATH "The 1.06 game data for the golden tests")
target_compile_definitions(d2k_tests PRIVATE
    D2K_GAMEDATA_DIR="${D2K_GAMEDATA_DIR}"
    D2K_REPO_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME d2k_tests COMMAND d2k_tests)
