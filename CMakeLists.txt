cmake_minimum_required(VERSION 3.6)
project(sequence_toolkit)

set(CMAKE_VERBOSE_MAKEFILE ON)

if(NOT APPLE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_BSD_SOURCE -D_XOPEN_SOURCE=500 -std=c11")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c11")

include_directories(include lib examples daemons mongoose)
add_subdirectory(lib)
add_subdirectory(examples)
add_subdirectory(daemons)
add_subdirectory(test_programs)
add_subdirectory(unit_tests)

set(HEADERS
        include/stk.h
        include/stk_common.h
        include/stk_data_flow.h
        include/stk_data_flow_api.h
        include/stk_env.h
        include/stk_env_api.h
        include/stk_ids.h
        include/stk_name_service.h
        include/stk_name_service_api.h
        include/stk_options.h
        include/stk_options_api.h
        include/stk_ports.h
        include/stk_rawudp.h
        include/stk_rawudp_api.h
        include/stk_sequence.h
        include/stk_sequence_api.h
        include/stk_service.h
        include/stk_service_api.h
        include/stk_service_group.h
        include/stk_service_group_api.h
        include/stk_sg_automation.h
        include/stk_sg_automation_api.h
        include/stk_smartbeat.h
        include/stk_smartbeat_api.h
        include/stk_sync.h
        include/stk_sync_api.h
        include/stk_tcp.h
        include/stk_tcp_client.h
        include/stk_tcp_client_api.h
        include/stk_tcp_server.h
        include/stk_tcp_server_api.h
        include/stk_timer.h
        include/stk_timer_api.h
        include/stk_udp.h
        include/stk_udp_client.h
        include/stk_udp_client_api.h
        include/stk_udp_listener.h
        include/stk_udp_listener_api.h
        )

SET(MONGOOSE_SOURCES mongoose/mongoose.c mongoose/mongoose.h)
add_library(mongoose ${MONGOOSE_SOURCES})

install (DIRECTORY include DESTINATION include)
