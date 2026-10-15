add_executable(
  lansocks-helper
  tunnel_helper.cpp
  route_manager.cpp
  dns_manager.cpp
)

target_compile_features(lansocks-helper PRIVATE cxx_std_17)
target_compile_options(lansocks-helper PRIVATE /utf-8)
target_compile_definitions(
  lansocks-helper
  PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
target_include_directories(lansocks-helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lansocks-helper PRIVATE iphlpapi ws2_32 shell32 ole32 dnsapi)
set_target_properties(
  lansocks-helper
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runner/$<CONFIG>/data"
    OUTPUT_NAME "lansocks-helper"
)
