add_executable(fohcore_tests test_multipos.cpp test_routing.cpp test_core.cpp test_bad_input.cpp test_dsp_edges.cpp test_format.cpp test_target.cpp test_session.cpp test_splcal.cpp test_room_sim.cpp)
find_package(Threads REQUIRED)
target_link_libraries(fohcore_tests PRIVATE fohcore foh_sanitizers Catch2::Catch2WithMain Threads::Threads)
target_compile_definitions(fohcore_tests PRIVATE FOH_TARGET_DATA="${CMAKE_SOURCE_DIR}/resources/targets" FOH_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
include(CTest)
include(Catch)
catch_discover_tests(fohcore_tests)

# Workspace layout model (app/Source/LayoutModel.cpp is pure C++17, no JUCE)
add_executable(fohws_tests test_layout.cpp ${CMAKE_SOURCE_DIR}/app/Source/LayoutModel.cpp)
target_include_directories(fohws_tests PRIVATE ${CMAKE_SOURCE_DIR}/app/Source)
target_link_libraries(fohws_tests PRIVATE fohcore foh_sanitizers Catch2::Catch2WithMain)
catch_discover_tests(fohws_tests)

# Synthetic golden WAVs are generated only in the build tree.
find_package(Python3 REQUIRED COMPONENTS Interpreter NumPy)
set(golden_dir "${CMAKE_CURRENT_BINARY_DIR}/golden")
set(golden_outputs "${golden_dir}/manifest.tsv")
set(golden_names
  pink_-12dBrms_s1 white_-12dBrms_s2 sine_1k_0dBFS sine_100_-20dBFS
  logsweep_20-20k_2s_-6dBFS impulse_at100 silence_1s pair_int230
  pair_frac4.80ms pair_frac4.80ms_snr10 pair_frac4.80ms_snr-10 pair_frac4.80ms_verb
  pair_inv230 pair_refl2ms_-3dB_+8ms pair_twospike3ms_4.5ms_-0.5dB
  pair_swapped4.80ms pair_long200ms pair_sub_bp30-120_12.5ms
  clip_sine1k_+6dB dc_0.5 dc_offset_pink)
foreach(rate IN ITEMS 44100 48000 96000)
  foreach(name IN LISTS golden_names)
    list(APPEND golden_outputs "${golden_dir}/${name}_${rate}.wav")
  endforeach()
endforeach()
foreach(length IN ITEMS 0 1 63 64 65 4096)
  list(APPEND golden_outputs "${golden_dir}/edge_len${length}_48000.wav")
endforeach()
add_custom_command(OUTPUT ${golden_outputs}
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/gen_golden.py" "${golden_dir}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/gen_golden.py"
  COMMENT "Generating synthetic golden signals"
  VERBATIM)
add_custom_target(golden_signals DEPENDS ${golden_outputs})
add_executable(golden_tests test_golden.cpp)
add_dependencies(golden_tests golden_signals)
target_link_libraries(golden_tests PRIVATE fohcore foh_sanitizers Catch2::Catch2WithMain)
target_compile_definitions(golden_tests PRIVATE FOH_GOLDEN_DATA="${golden_dir}")
add_test(NAME golden_tests COMMAND golden_tests)
