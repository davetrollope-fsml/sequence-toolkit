# Copyright Dave Trollope 2014
# This source code is not to be distributed without agreement from
# D. Trollope
#
# This file implements a basic client and is intended to be a simple introduction
# to the STK for client type applications. It is intended to be used with simple_server
# which reflects sequences back to clients.

# System imports
import getopt, sys, time, random

# Sequence Toolkit imports - stk_env must be first
from stk_env import *
from stk_options import *
from stk_service_group import *
from stk_service import *
from stk_tcp_client import *
from stk_data_flow import *
from stk_sg_automation import *
from stk_name_service import *

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
signal.signal(signal.SIGINT, signal_handler)

# Class to collect command line options

# command line options provided - set in process_cmdline()
class cmdopts:
	group_name = "Simple Server Service Group"
	linger = 300 # Keep names around for 5 mins after death - 5 mins by default
	quiet = False
	callbacks = 1
	name_server_ip = "127.0.0.1"
	name_server_port = "20002"
	name_server_protocol = "tcp"
	server_ip = "127.0.0.1"
	server_port = "29312"
	name = "TEST"
	meta_data = {}
	ft_state = "active"

opts = cmdopts()

def usage():
	print("Usage: simple_name_registration.py [options] <name>")
	print("       -i <ip[:port]>            : IP and port of server")
	print("       -M <id,value>             : meta data (integer id, string value)")
	print("       -F <active|backup>        : Fault tolerant state to register with name")
	print("       -G <name>                 : Group Name for services")
	print("       -L <linger sec>           : Time name should exist after connection to name server dies")
	print("       -c #                      : # of callbacks")
	print("       -R <[protocol:]ip[:port]> : IP and port of name server")
	print("       -h                        : This help!")
	print("       -q                        : Quiet")

# Process command line options
def process_cmdline():
	try:
		gopts, args = getopt.getopt(sys.argv[1:], "hqG:L:c:F:i:m:M:R:", ["help", "quiet", "group-name=", "linger="])
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
		elif o in ("-F"):
			opts.ft_state = a
		elif o in ("-G", "--group-name"):
			opts.group_name = a
		elif o in ("-L", "--linger"):
			opts.linger = int(a)
		elif o in ("-c"):
			opts.callbacks = int(a)
		elif o in ("-M"):
			metadata = a.split(',')
			opts.meta_data[int(metadata[0])] = metadata[1]
		elif o in ("-i"):
			server_ip = a.split(':')
			opts.server_ip = server_ip[0]
			if len(server_ip) > 1:
				opts.server_port = server_ip[1]
		elif o in ("-R"):
			p=stk_protocol_def_t()
			a=stk_data_flow_parse_protocol_str(p,a)
			if p.ip != '':
				opts.name_server_ip = p.ip
			if p.port != '':
				opts.name_server_port = p.port
			if p.protocol != '':
				opts.name_server_protocol = p.protocol
	if len(args) == 0:
		print "Missing name"
		usage()
		sys.exit(5)
	opts.name = args[0]

# Process command line options
process_cmdline()

df = None
df_opts = None
dispatcher_cbs = None
meta_data_seq = None
stkbase = None
envopts = None
expired = False

def cleanup():
	try:
		# destroy the data flow, sequence, service group and environment
		if df:
			df.close()
		if df_opts:
			df_opts.remove_dispatcher_fd_cbs()
			df_opts.close()
		if meta_data_seq:
			meta_data_seq.close()
		# And get rid of the environment, we are done!
		if stkbase:
			stkbase.close()
		# Close the callback objects
		if dispatcher_cbs:
			dispatcher_cbs.close()
		if envopts:
			# Now free the options that were built
			# Because there was nested options, we must free each nest individually
			# because there is no stored indication which options are nested
			envopts.remove_dispatcher_wakeup_cb()
			nsopts = envopts.find_option("name_server_options")
			stk_env.remove_name_server_dispatcher_cbs(nsopts,"name_server_data_flow")
			nsopts.free_sub_option("name_server_data_flow_options")
			envopts.free_sub_option("name_server_options")
			envopts.close()
	except Exception, e:
		print "Exception occurred during cleanup: " + str(e)

# Create the STK environment - can't do anything without one
opts_dict = {
	"name_server_options": {
		"name_server_data_flow_protocol": opts.name_server_protocol,
		"name_server_data_flow_options": {
			"data_flow_name": """%(protocol)s name server socket for simple_name_registration""" % {"protocol": opts.name_server_protocol},
			"data_flow_id": 10000,
			"destination_address": opts.name_server_ip,
			"destination_port": opts.name_server_port
		}
	}
}
if opts.group_name:
	opts_dict["name_server_options"]["group_name"] = opts.group_name
print "AA " + str(opts_dict)
envopts = stk_options(opts_dict)

# Class containing callbacks for the name service
class name_service_cb(stk_callback):
	def __init__(self):
		stk_callback.__init__(self)
	def close(self):
		stk_callback.close(self)
	def print_meta_data(self,seq,data,user_type,clientd):
		print "Meta data type " + str(user_type) + " sz " + str(len(data))
	def name_info_cb(self,name_info,server_info,app_info,cb_type):
		global cbs_rcvd
		cbs_rcvd += 1
		if cb_type == STK_NS_REQUEST_EXPIRED:
			print "Request expired on name " + name_info.name()
			global expired
			expired = True
			return
		ip = name_info.ip(0)
		print "Received info on name " + name_info.name() + ", IP " + ip.ipstr + " Port " + ip.portstr + " Protocol " + ip.protocol
		global stkbase
		meta_data = name_info.meta_data(stkbase)
		if meta_data != None:
			meta_data.iterate(self.print_meta_data,None)

# Class containing callbacks for the dispatcher - this is how we receive data
class dispatcher_cb(stk_callback):
	def __init__(self):
		stk_callback.__init__(self)
	def close(self):
		stk_callback.close(self)
	def process_data(self,rcvchannel,rcv_seq): # Callback to receive data
		global cbs_rcvd
		cbs_rcvd += 1
	def process_name_response(self,rcvchannel,rcv_seq): # Callback to receive name info
		try:
			stk_name_service.invoke(rcv_seq)
		except Exception, e:
			print "Exception occurred processing received data: " + str(e)

dispatcher_cbs = dispatcher_cb()

# Let the environment automatically add and remove fds for these data flows to the dispatcher
stk_env.append_name_server_dispatcher_cbs(envopts,"name_server_data_flow")

# Create an STK environment.
stkbase = stk_env(envopts)

cbs_rcvd = 0

meta_data_seq = stk_sequence.stk_sequence(stkbase,"meta data sequence",0,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,None)
if meta_data_seq == None:
	print "Failed to create the client sequence"
	cleanup()
	exit(5)

for k, v in opts.meta_data.iteritems():
	print k, v
	meta_data_seq.copy_string_to(v,k);

# Register name
print "Registering info on name " + opts.name
name_opts_dict = {
	"connect_address": opts.server_ip,
	"connect_port": opts.server_port,
	"fault_tolerant_state": opts.ft_state
}
name_options = stk_options(name_opts_dict)
name_options.append_sequence("meta_data_sequence",meta_data_seq)

try:
	# Create name service callbacks object and register name
	name_info_cb = name_service_cb()

	rc = stk_name_service.register_name(stkbase, opts.name, opts.linger, 1000, name_info_cb, None, name_options);
	if rc != STK_SUCCESS:
		print "Failed to request name"
		cleanup()
		sys.exit(5);
except Exception, e:
	print "Exception occurred trying to register name: " + str(e)
	cleanup()
	sys.exit(5)

# Run the client dispatcher to receive data until we get all the responses
while cbs_rcvd < opts.callbacks and expired == False:
	try:
		print "received " + str(cbs_rcvd) + " callbacks"
		stkbase.client_dispatcher_timed(dispatcher_cbs,1000);
	except Exception, e:
		print "Exception occurred waiting for data to arrive: " + str(e)

print "Received " + str(cbs_rcvd) + " name registration callbacks"

print "Waiting 5 seconds before closing"
stkbase.client_dispatcher_timed(dispatcher_cbs,5000);

# The dispatcher returned, cleanup everything
cleanup()

