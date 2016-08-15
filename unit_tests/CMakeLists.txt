add_executable(sync_test sync_test.c)

target_link_libraries(sync_test stk)

install (TARGETS sync_test DESTINATION unit_tests)
