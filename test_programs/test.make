#INCLUDES=-I../openpa-1.0.4/src -I../include -I.. -I../examples
INCLUDES=-I../include -I../examples
WARNFLAGS=-Wno-unused-parameter
CFLAGS=$(INCLUDES) -fPIC -g -pedantic -Wextra -std=c99 -D_BSD_SOURCE -D_XOPEN_SOURCE=500 $(WARNFLAGS)

LDLIBS=-L../lib -lstk -lpthread

TESTPROGS=test_types create_env_test create_service_test create_service_group_test check_service_group_state_test \
			create_sequence_test sequence_iterator_test \
			service_group_auto_svc_test \
			timer_test \
			create_lite_pkg_test \
			service_state_names \
			sequence_tests \
			name_service_tests \
			options_tests \
			rawudp_data_flow_test \
			udp_data_flow_test \
			tcp_data_flow_test

UNAME_S=$(shell uname)


runall: $(TESTPROGS)
	./test_types
	./create_env_test
	./create_service_test
	./create_service_group_test
	./create_sequence_test
	./sequence_iterator_test
	./check_service_group_state_test
	./service_group_auto_svc_test
	./service_state_names
	./sequence_tests
	./options_tests
	./timer_test
	bash -c "(../daemons/stknamed & sleep 2; ./name_service_tests; kill %1)"
	bash -c "(./tcp_data_flow_test server & sleep 2; ./tcp_data_flow_test; kill %1)"
	bash -c "(./rawudp_data_flow_test server & sleep 2; ./rawudp_data_flow_test; kill %1)"
	bash -c "(./rawudp_data_flow_test multicast-server & sleep 2; ./rawudp_data_flow_test multicast; kill %1)"
	bash -c "(./udp_data_flow_test server & sleep 2; ./udp_data_flow_test; kill %1)"
	bash -c "(./udp_data_flow_test multicast-server & sleep 2; ./udp_data_flow_test multicast; kill %1)"

runlite: $(TESTPROGS)
	./create_lite_pkg_test

runvalgrindall: $(TESTPROGS)
	valgrind --leak-check=full --log-file=create_env_test.valg.log ./create_env_test
	valgrind --leak-check=full --log-file=create_service_test.valg.log ./create_service_test
	valgrind --leak-check=full --log-file=create_service_group_test.valg.log ./create_service_group_test
	valgrind --leak-check=full --log-file=create_sequence_test.valg.log ./create_sequence_test
	valgrind --leak-check=full --log-file=sequence_iterator_test.valg.log ./sequence_iterator_test
	valgrind --leak-check=full --log-file=check_service_group_state_test.valg.log ./check_service_group_state_test
	valgrind --leak-check=full --log-file=service_group_auto_svc_test.valg.log ./service_group_auto_svc_test
	valgrind --leak-check=full --log-file=service_state_names.valg.log ./service_state_names
	valgrind --leak-check=full --log-file=sequence_tests.valg.log ./sequence_tests
	valgrind --leak-check=full --log-file=options_tests.valg.log ./options_tests
	valgrind --leak-check=full --log-file=timer_test.valg.log ./timer_test
	bash -c "(valgrind --leak-check=full --log-file=stknamed.valg.log ../daemons/stknamed & sleep 2; \
	                    valgrind --leak-check=full --log-file=name_service_tests.valg.log ./name_service_tests; kill %1)"
	bash -c "(valgrind --leak-check=full --log-file=tcp_data_flow_test_server.valg.log ./tcp_data_flow_test server & sleep 2; \
	                    valgrind --leak-check=full --log-file=tcp_data_flow_test.valg.log ./tcp_data_flow_test; kill %1)"
	bash -c "(valgrind --leak-check=full --log-file=rawudp_data_flow_test_server.valg.log ./rawudp_data_flow_test server & sleep 2; \
	                    valgrind --leak-check=full --log-file=rawudp_data_flow_test.valg.log ./rawudp_data_flow_test; kill %1)"
	bash -c "(valgrind --leak-check=full --log-file=rawudp_data_flow_test_server.valg.log ./rawudp_data_flow_test multicast-server & sleep 2; \
	                    valgrind --leak-check=full --log-file=rawudp_data_flow_test.valg.log ./rawudp_data_flow_test multicast; kill %1)" \
	bash -c "(valgrind --leak-check=full --log-file=udp_data_flow_test_server.valg.log ./udp_data_flow_test server & sleep 2; \
	                    valgrind --leak-check=full --log-file=udp_data_flow_test.valg.log ./udp_data_flow_test; kill %1)"
	bash -c "(valgrind --leak-check=full --log-file=udp_data_flow_test_server.valg.log ./udp_data_flow_test multicast-server & sleep 2; \
	                    valgrind --leak-check=full --log-file=udp_data_flow_test.valg.log ./udp_data_flow_test multicast; kill %1)"

