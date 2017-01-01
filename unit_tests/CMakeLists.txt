add_executable(sync_test sync_test.c)

if(APPLE)
  SET(LIB_DEPS stk)
else()
  SET(LIB_DEPS stk pthread dl)
endif()

target_link_libraries(sync_test ${LIB_DEPS})

install (TARGETS sync_test DESTINATION unit_tests)
