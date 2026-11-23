add_executable(d2k-tool main.cpp png_writer.cpp)
target_link_libraries(d2k-tool PRIVATE d2k_formats d2k_platform d2k_campaign d2k_sim d2k_ui d2k_render d2k_i18n)
