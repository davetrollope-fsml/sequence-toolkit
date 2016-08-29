# Copyright Dave Trollope 2013
# This source code is not to be distributed without agreement from
# D. Trollope
#
# This file implements a basic service for monitoring purposes only and is intended to be
# a simple demonstration of how STK can be added to existing applications for monitoring.

# System imports
import getopt, sys, time, random

# Sequence Toolkit imports
from stk_env import *
from stk_options import *
from stk_service import *

# Class to collect command line options

# command line options provided - set in process_cmdline()
class cmdopts:
	service_name = "basic python monitored service"
	end_threshold = 1000000
opts = cmdopts()

# Process command line options
def process_cmdline():
	try:
		gopts, args = getopt.getopt(sys.argv[1:], "ht:S:", ["help", "threshold=", "service-name="])
	except getopt.GetoptError:
		# print help information and exit:
		usage()
		sys.exit(2)
	for o, a in gopts:
		if o in ("-t", "--threshold"):
			opts.end_threshold = int(a)
		elif o in ("-h", "--help"):
			usage()
			sys.exit()
		elif o in ("-S", "--service-name"):
			opts.service_name = a

def usage():
	print("Usage: monitored_service.py [options]");
	print("       -t <threshold> : End threshold (iterations)");
	print("       -S <name>      : Service Name");

# Process command line options
process_cmdline()

svc = None
stkbase = None
envopts = None

def cleanup():
	try:
		# destroy the service and environment
		if svc:
			svc.close()
		# And get rid of the environment, we are done!
		if stkbase:
			stkbase.close()
		if envopts:
			# Now free the options that were built
			# Because there was nested options, we must free each nest individually
			# because there is no stored indication which options are nested
			envopts.remove_dispatcher_wakeup_cb()
			envopts.free_sub_option("monitoring_data_flow_options")
			envopts.free_sub_option("name_server_options")
			envopts.close()
	except Exception, e:
		print "Exception occurred during cleanup: " + str(e)


# Create the STK environment - can't do anything without one
# By configuring the IP/Port of the monitoring service when we create the environment
# all services created within it will automatically use the monitoring service unless
# overridden.
opts_dict = {
	"name_server_options": {
		"data_flow_name": "tcp name server socket for monitored_service",
		"data_flow_id": 10000,
		"connect_address": "127.0.0.1",
		"connect_port": 20002,
	},
	"monitoring_data_flow_options": {
		"data_flow_name": "tcp monitoring socket for monitored_service",
		"data_flow_id": 10001,
		"connect_address": "127.0.0.1",
		"connect_port": 20001,
		"nodelay": 1
	}
}
# The formatted string of options are built in to an options table and then passed to stk_create_env
envopts = stk_options(opts_dict)
stkbase = stk_env(envopts)

try:
	# Create our simple client service using a random ID, monitoring is inherited from the environment
	random.seed()
	svc_id = random.randint(0,0x7fffffffffffffff)

	svc = stk_service(stkbase,opts.service_name,
		svc_id,STK_SERVICE_TYPE_DATA,None)
	print "Using random service ID: " + str(svc_id)
except Exception, e:
	print "Exception occurred creating service: " + str(e)
	cleanup()
	sys.exit(5)

# Set this service to a running state so folks know we are in good shape
svc.set_state(STK_SERVICE_STATE_RUNNING)

# Start the application logic updating the checkpoint of the service and
# invoke the timer handler to allow heartbeats to be sent
x = 1
checkpoint = 0
 
while 1:
	# Do some application logic
	x = x * 2
	print "The new value of x is " + repr(x)

	try:
		# Update this service's checkpoint so others know we are doing sane things
		svc.update_smartbeat_checkpoint(checkpoint)
		checkpoint += 1
		stkbase.dispatch_timer_pools(0)
	except Exception, e:
		print "Exception occurred updating smartbeat and dispatching timers: " + str(e)

	# See if its time to end
	if opts.end_threshold > 0 and checkpoint > opts.end_threshold:
		break
	time.sleep(1)

# Set this service to state 'stopping' so folks know we are in ending
svc.set_state(STK_SERVICE_STATE_STOPPING)

# The dispatcher returned, cleanup everything
cleanup()

print opts.service_name + " hit its threshold of " + repr(opts.end_threshold)
