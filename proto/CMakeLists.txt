add_library(bridge_protocol STATIC
    bridge.pb.c
    ../third_party/nanopb/pb_common.c
    ../third_party/nanopb/pb_encode.c
    ../third_party/nanopb/pb_decode.c)
target_include_directories(bridge_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nanopb)
