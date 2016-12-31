
set(CMAKE_VERBOSE_MAKEFILE ON)

set(LIB_SOURCES
        PLists.c
        PLists.h
        setenv_stk
        stk_assert_log.h
        stk_data_flow.c
        stk_df_internal.h
        stk_env.c
        stk_internal.h
        stk_mixed_connection.c
        stk_mixed_connection.h
        stk_mixed_connection_api.h
        stk_name_service.c
        stk_options.c
        stk_rawudp.c
        stk_sequence.c
        stk_service.c
        stk_service_group.c
        stk_sg_automation.c
        stk_sga_internal.h
        stk_smartbeat.c
        stk_sync.c
        stk_tcp_client.c
        stk_tcp_internal.h
        stk_tcp_server.c
        stk_timer.c
        stk_udp_client.c
        stk_udp_internal.h
        stk_udp_listener.c
        )
add_library(stk SHARED ${HEADERS} ${LIB_SOURCES})

install (TARGETS stk DESTINATION lib)
