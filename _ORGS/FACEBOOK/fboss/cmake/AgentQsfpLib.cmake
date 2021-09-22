# CMake to build libraries and binaries in fboss/agent/hw/bcm

# In general, libraries and binaries in fboss/foo/bar are built by
# cmake/FooBar.cmake

add_library(qsfp_service_client
    fboss/qsfp_service/lib/QsfpClient.cpp
    fboss/qsfp_service/lib/oss/QsfpClient.cpp
)

target_link_libraries(qsfp_service_client
    qsfp_cpp2
)

add_library(qsfp_cache
    fboss/qsfp_service/lib/QsfpCache.cpp
)

target_link_libraries(qsfp_cache
    qsfp_service_client
    ctrl_cpp2
    transceiver_cpp2
    alert_logger
)

add_library(qsfp_config_parser
    fboss/qsfp_service/lib/QsfpConfigParserHelper.cpp
)

target_link_libraries(qsfp_config_parser
    transceiver_cpp2
    qsfp_config_cpp2
)
