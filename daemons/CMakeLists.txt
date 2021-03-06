SET(DISPATCHER_SOURCES ../examples/eg_dispatcher.c ../examples/eg_dispatcher_api.h)
SET(NAMED_SOURCES stknamed.c stk_name_store.c stk_subscription_store.c)
SET(HTTPD_SOURCES stkhttpd.c stk_httpcontent.c)
if(APPLE)
  SET(LIB_DEPS stk)
else()
  SET(LIB_DEPS stk pthread dl)
endif()

add_executable(stknamed ${NAMED_SOURCES} ${DISPATCHER_SOURCES} stk_named_main.c)
add_executable(stkhttpd ${HTTPD_SOURCES} ${DISPATCHER_SOURCES} stk_httpd_main.c)
add_executable(stkd stkd.c ${NAMED_SOURCES} ${HTTPD_SOURCES} ${DISPATCHER_SOURCES})

target_link_libraries(stkd stk ${LIB_DEPS} mongoose)
target_link_libraries(stkhttpd ${LIB_DEPS} mongoose)
target_link_libraries(stknamed ${LIB_DEPS} )

install (TARGETS stkd DESTINATION daemons)
install (TARGETS stkhttpd DESTINATION daemons)
install (TARGETS stknamed DESTINATION daemons)

file(GLOB PARTIALS "*.phtml")
file(COPY ${PROJECT_SOURCE_DIR}/daemons/stkd.cfg DESTINATION ${PROJECT_BINARY_DIR}/daemons)
file(COPY ${PARTIALS} DESTINATION ${PROJECT_BINARY_DIR}/daemons)
file(COPY ${PROJECT_SOURCE_DIR}/daemons/images DESTINATION ${PROJECT_BINARY_DIR}/daemons)
file(COPY ${PROJECT_SOURCE_DIR}/daemons/js DESTINATION ${PROJECT_BINARY_DIR}/daemons)

