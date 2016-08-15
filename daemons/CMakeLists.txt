SET(DISPATCHER_SOURCES ../examples/eg_dispatcher.c ../examples/eg_dispatcher_api.h)
SET(NAMED_SOURCES stknamed.c stk_name_store.c stk_subscription_store.c)
SET(HTTPD_SOURCES stkhttpd.c stk_httpcontent.c)

add_executable(stknamed ${NAMED_SOURCES} ${DISPATCHER_SOURCES} stk_named_main.c)
add_executable(stkhttpd ${HTTPD_SOURCES} ${DISPATCHER_SOURCES} stk_httpd_main.c)
add_executable(stkd stkd.c ${NAMED_SOURCES} ${HTTPD_SOURCES} ${DISPATCHER_SOURCES})

target_link_libraries(stkd stk mongoose)
target_link_libraries(stkhttpd stk mongoose)
target_link_libraries(stknamed stk)

install (TARGETS stkd DESTINATION daemons)
install (TARGETS stkhttpd DESTINATION daemons)
install (TARGETS stknamed DESTINATION daemons)
