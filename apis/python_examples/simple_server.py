# Copyright Dave Trollope 2014
# This source code is not to be distributed without agreement from
# D. Trollope
#
# This example demonstrates how to code up a simple sequence server
# which accepts connections using the TCP data flow module,
# manages a service group and responds to sequences
# from a client.

# System imports
import getopt, sys, time, random

# Sequence Toolkit imports - stk_env must be first
from stk_env import *
from stk_options import *
from stk_service_group import *
from stk_service import *
from stk_tcp_server import *
from stk_data_flow import *
from stk_sg_automation import *

# Add a handler for CTRL-C to gracefully exit
import signal
import sys
ending = 0
def signal_handler(signal, frame):
	global ending
	if ending > 0:
		sys.exit(0)
	ending += 1
	stkbase.stop_dispatcher()
	stkbase.terminate_dispatcher()
signal.signal(signal.SIGINT, signal_handler)

# Class to collect command line options

# command line options provided - set in process_cmdline()
class cmdopts:
	group_name = "Simple Server Service Group"
	quiet = False
	bind_ip = "0.0.0.0"
	bind_port = "29312"
	monitor_ip = "127.0.0.1"
	monitor_port = "20001"
	monitor_protocol = "tcp"
	name_server_ip = "127.0.0.1"
	name_server_port = "20002"
	name_server_protocol = "tcp"

opts = cmdopts()

# Process command line options
def process_cmdline():
	try:
		gopts, args = getopt.getopt(sys.argv[1:], "hqG:B:m:R:", ["help", "quiet", "group-name="])
	except getopt.GetoptError:
		# print help information and exit:
		usage()
		sys.exit(2)
	for o, a in gopts:
		if o in ("-q", "--quiet"):
			opts.quiet = True
		elif o in ("-h", "--help"):
			usage()
			sys.exit()
		elif o in ("-B"):
			bind_ip = a.split(':')
			opts.bind_ip = bind_ip[0]
			if bind_ip.count == 2:
				opts.bind_port = bind_ip[1]
		elif o in ("-G", "--group-name"):
			opts.group_name = a
		elif o in ("-m"):
			p=stk_protocol_def_t()
			a=stk_data_flow_parse_protocol_str(p,a)
			if p.ip != '':
				opts.monitor_ip = p.ip
			if p.port != '':
				opts.monitor_port = p.port
			if p.protocol != '':
				opts.monitor_protocol = p.protocol
		elif o in ("-R"):
			p=stk_protocol_def_t()
			a=stk_data_flow_parse_protocol_str(p,a)
			if p.ip != '':
				opts.name_server_ip = p.ip
			if p.port != '':
				opts.name_server_port = p.port
			if p.protocol != '':
				opts.name_server_protocol = p.protocol

def usage():
	print("Usage: simple_server.py [options]")
	print("       -h                        : This help!")
	print("       -q                        : Quiet")
	print("       -B ip[:port]              : IP and port to be bound (default: 0.0.0.0:29312)")
	print("       -G <name>                 : Group Name for services")
	print("       -m <[protocol:]ip[:port]> : IP and port of monitor")
	print("       -R <[protocol:]ip[:port]> : IP and port of name server")

# Process command line options
process_cmdline()

# The following are used during cleanup
df = None
df_opts = None
svcgrp = None
svcgrp_opts = None
stkbase = None
envopts = None
dispatcher_cbs = None
service_cbs = None

def cleanup():
	try:
		# destroy the data flow, sequence, service group and environment
		if df:
			df.close()
		if df_opts:
			df_opts.remove_dispatcher_fd_cbs()
			df_opts.close()
		if svcgrp:
			svcgrp.close()
		if svcgrp_opts:
			stk_service_group.remove_service_cb_option(stkbase,svcgrp_opts,service_cbs)
			svcgrp_opts.remove_data_flow("listening_data_flow")
			svcgrp_opts.close()
		# Close the callback objects
		if service_cbs:
			service_cbs.close()
		# And get rid of the environment, we are done!
		if stkbase:
			stkbase.close()
		if dispatcher_cbs:
			#dispatcher_cbs.caller().close()
			dispatcher_cbs.close()
		if envopts:
			# Now free the options that were built
			# Because there was nested options, we must free each nest individually
			# because there is no stored indication which options are nested
			envopts.remove_dispatcher_wakeup_cb()
			nsopts = envopts.find_option("name_server_options")
			stk_env.remove_name_server_dispatcher_cbs(nsopts,"name_server_data_flow")
			nsopts.free_sub_option("name_server_data_flow_options")
			stk_env.remove_monitoring_dispatcher_cbs(envopts,"monitoring_data_flow")
			envopts.free_sub_option("monitoring_data_flow_options")
			envopts.free_sub_option("name_server_options")
			envopts.close()
	except Exception, e:
		print "Exception occurred cleaning up: " + str(e)


# Create the STK environment - can't do anything without one
# By configuring the IP/Port of the monitoring service when we create the environment
# all services created within it will automatically use the monitoring service unless
# overridden.
opts_dict = {
	"name_server_options": {
		"name_server_data_flow_protocol": opts.name_server_protocol,
		"name_server_data_flow_options": {
			"data_flow_name": """%(protocol)s name server socket for simple_server""" % {"protocol": opts.name_server_protocol },
			"data_flow_id": 10000,
			"destination_address": opts.name_server_ip,
			"destination_port": opts.name_server_port
		}
	},
	"monitoring_data_flow_protocol": opts.monitor_protocol,
	"monitoring_data_flow_options": {
		"data_flow_name": """%(protocol)s monitoring socket for simple_server""" % {"protocol": opts.monitor_protocol },
		"data_flow_id": 10001,
		"destination_address": opts.monitor_ip,
		"destination_port": opts.monitor_port,
		"nodelay": 1
	}
}
envopts = stk_options(opts_dict)

# Let the environment automatically add and remove fds for these data flows to the dispatcher
stk_env.append_name_server_dispatcher_cbs(envopts,"name_server_data_flow")
stk_env.append_monitoring_dispatcher_cbs(envopts,"monitoring_data_flow")

# Create an STK environment. Since we are using the example listening dispatcher,
# set an option for the environment to ensure the dispatcher wakeup API is called.
envopts.append_dispatcher_wakeup_cb()
stkbase = stk_env(envopts)

# Class containing callbacks for services (added, removed and changing state)
class app_service_cb(stk_callback):
	def __init__(self):
		stk_callback.__init__(self)
	def close(self):
		stk_callback.close(self)
	def added_cb(self,svcgrp,svc,state): # Service added callback
		print "Service " + svc.name() + " added to service group " + svcgrp.name() + " [state " + str(state) + "]"
	def removed_cb(self,svcgrp,svc,state): # Service removed callback
		print "Service " + svc.name() + " removed from service group " + svcgrp.name() + " [state " + str(state) + "]"
	def state_change_cb(self,svc,old_state,new_state): # Service changing state callback
		old_state_str = svc.state_str(old_state);
		new_state_str = svc.state_str(new_state);
		print "Service '" + svc.name() + "' changed from state " + str(old_state_str) + " to " + str(new_state_str);
	def smartbeat_cb(self,svcgrp,svc,smartbeat):
		try:
			print "Service '" + svc.name() + "' group '" + svcgrp.name() + "' smartbeat received, checkpoint " + str(smartbeat.checkpoint())
		except Exception, e:
			print str(e)

# Class containing callbacks for the dispatcher - this is how we receive data
class dispatcher_cb(stk_callback):
	def __init__(self):
		stk_callback.__init__(self)
	def close(self):
		stk_callback.close(self)
	def process_seq_segment(self,seq,data,user_type,clientd):
		if opts.quiet == False:
			print "Sequence " + str(seq.id()) + " Received " + str(len(data)) + " bytes of type " + str(user_type)
			if len(data) >= 4:
				sz = len(data)
				print 'Bytes: %02x %02x %02x %02x ... %02x %02x %02x %02x' % (ord(data[0]),ord(data[1]),ord(data[2]),ord(data[3]),ord(data[sz - 4]),ord(data[sz - 3]),ord(data[sz - 2]),ord(data[sz - 1]))
	num = 0
	def process_data(self,rcvchannel,rcv_seq): # Callback to receive data
		try:
			if rcv_seq.type() != STK_SEQUENCE_TYPE_DATA:
				svcgrp.invoke(rcv_seq)
			if opts.quiet == False:
				print "data flow " + str(rcvchannel.id()) + ": Number of elements in received sequence: " + str(rcv_seq.count()) + " Sequence type: " + str(rcv_seq.type())
			if rcv_seq.type() != STK_SEQUENCE_TYPE_DATA:
				return
			# Call process_seq_segment() on each element in the sequence
			rcv_seq.iterate(self.process_seq_segment,None)
		except Exception, e:
			print "Exception occurred processing received data: " + str(e)

		try:
			ret_seq = stk_sequence(rcv_seq.env(),"simple_server_return_data",0x7edcba90,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,None)

			retbuf = []
			i = 0
			while i < 10:
				retbuf.append(i)
				i += 1

			ret_seq.copy_array_to(retbuf,self.__class__.num);
			self.__class__.num += 1

			rcvchannel.send(ret_seq,STK_TCP_SEND_FLAG_NONBLOCK)
		except Exception, e:
			print "Exception occurred returning data: " + str(e)
	def process_name_response(self,rcvchannel,rcv_seq): # Callback to receive name info
		pass

try:
	# Create the options for the server data flow
	print "Creating server data flow"

	df_opts_dict = {
		"bind_address": opts.bind_ip,
		"bind_port": opts.bind_port,
		"nodelay": 1,
		"send_buffer_size": 800000,
		"receive_buffer_size": 16000000,
		"reuseaddr": 1
	}
	df_opts = stk_options(df_opts_dict)
	df_opts.append_dispatcher_fd_cbs(None)

	# Create the TCP server data flow (aka a listening socket)
	df = stk_tcp_server(stkbase,"tcp server socket for simple_server", 29190, df_opts)
	if df == None:
		print "Failed to create the server data flow"
		cleanup()
		sys.exit(5)
except Exception, e:
	print "Exception occurred trying to create server data flow: " + str(e)
	cleanup()
	sys.exit(5)

print "Server data flow created"

try:
	# Create service callbacks object and add to service group options
	service_cbs = app_service_cb()

	svcgrp_opts = stk_options("")
	svcgrp_opts.append_data_flow("listening_data_flow",df)
	svccb = stk_service_group.add_service_cb_option(stkbase,svcgrp_opts,service_cbs)

	# Create the service group that client services will be added to as they are discovered.
	# Also, register callbacks so we can be notified when services are added and removed.
	svcgrp = stk_service_group(stkbase, opts.group_name, 1000, svcgrp_opts)
except Exception, e:
	print "Exception occurred trying to create service group: " + str(e)
	cleanup()
	sys.exit(5)

try:
	# Run the example listening dispatcher to accept data flows from clients
	# and receive data from them. This example does this inline, but an
	# application might choose to invoke this on another thread.
	# 
	# The dispatcher only returns when a shutdown is detected.
	dispatcher_cbs = dispatcher_cb()
	stkbase.listening_dispatcher(df,svcgrp,dispatcher_cbs)
except Exception, e:
	print "Exception occurred dispatching: " + str(e)

# The dispatcher returned, cleanup everything
cleanup()

