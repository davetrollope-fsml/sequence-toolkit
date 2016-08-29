import sys
from stk_name_service import *

def name_lookup_and_dispatch(stkbase,name,cb,opts,data,dispatcher_cbs,subscribe,cleanup):
	try:
		if subscribe:
			rc = stk_name_service.subscribe_to_name_info(stkbase,name,cb,None,None)
		else:
			rc = stk_name_service.request_name_info(stkbase,name,1000,cb,None,None)
		if rc != STK_SUCCESS:
			print "Failed to request name"
			cleanup()
			sys.exit(5);
	except Exception, e:
		print "Exception occurred trying to request name info: " + str(e)
		cleanup()
		sys.exit(5)

	# Run the client dispatcher to receive data until we get all the responses
	while (subscribe == False) and (data.cbs_rcvd < opts.callbacks and data.expired == 0):
		try:
			stkbase.client_dispatcher_timed(dispatcher_cbs,1000);
			print "Received " + str(data.cbs_rcvd) + " sequences, waiting for " + str(opts.callbacks)
		except Exception, e:
			print "Exception occurred waiting for data to arrive: " + str(e)

	if data.expired == 1 and data.cbs_rcvd == 1:
		print "Could not resolve " + name
		sys.exit(5);

