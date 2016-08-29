# Copyright Dave Trollope 2014
# This source code is not to be distributed without agreement from
# D. Trollope
#
# This file implements a basic client for the name service

# System imports
import getopt, sys, time, random

# Sequence Toolkit imports - stk_env must be first
from stk_env import *
from stk_options import *
from stk_service_group import *
from stk_service import *
from stk_name_service import *
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
signal.signal(signal.SIGINT, signal_handler)

# Class to collect command line options

# command line options provided - set in process_cmdline()
class cmdopts:
	group_name = None
	callbacks = 2
	name_server_ip = "127.0.0.1"
	name_server_port = "20002"
	name_server_protocol = "tcp"
	name = "TEST"
	subscribe = False

opts = cmdopts()

# Process command line options
def process_cmdline():
	try:
		gopts, args = getopt.getopt(sys.argv[1:], "hG:c:XR:", ["help", "group-name="])
	except getopt.GetoptError:
		# print help information and exit:
		usage()
		sys.exit(2)
	for o, a in gopts:
		if o in ("-h", "--help"):
			usage()
			sys.exit()
		elif o in ("-c"):
			opts.callbacks = int(a)
		elif o in ("-G", "--group-name"):
			opts.group_name = a
		elif o in ("-R"):
			p=stk_protocol_def_t()
			a=stk_data_flow_parse_protocol_str(p,a)
			if p.ip != '':
				opts.name_server_ip = p.ip
			if p.port != '':
				opts.name_server_port = p.port
			if p.protocol != '':
				opts.name_server_protocol = p.protocol
		elif o in ("-X"):
			opts.subscribe = True
	if len(args) == 0:
		print "Missing name"
		usage()
		sys.exit(5)
	opts.name = args[0]

def usage():
	print("Usage: simple_name_lookup.py [options] <name>")
	print("       -h                        : This help!")
	print("       -q                        : Quiet")
	print("       -c #                      : Number of callbacks")
	print("       -G <name>                 : Group Name for services")
	print("       -R <[protocol:]ip[:port]> : IP and port of name server")
	print("       -X                        : Subscribe mode\n")

# Process command line options
process_cmdline()

# The following are used during cleanup
df = None
df_opts = None
stkbase = None
envopts = None
dispatcher_cbs = None

def cleanup():
	try:
		# destroy the data flow, sequence, service group and environment
		if df:
			df.close()
		if df_opts:
			df_opts.close()
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
		print "Exception occurred cleaning up: " + str(e)


# Create the STK environment - can't do anything without one
opts_dict = {
	"name_server_options": {
		"name_server_data_flow_protocol": opts.name_server_protocol,
		"name_server_data_flow_options": {
			"data_flow_name": """%(protocol)s name server socket for simple_name_lookup""" % {"protocol": opts.name_server_protocol},
			"data_flow_id": 10000,
			"destination_address": opts.name_server_ip,
			"destination_port": opts.name_server_port
		}
	}
}
if opts.group_name:
	opts_dict["name_server_options"]["group_name"] = opts.group_name
envopts = stk_options(opts_dict)

# Let the environment automatically add and remove fds for these data flows to the dispatcher
stk_env.append_name_server_dispatcher_cbs(envopts,"name_server_data_flow")

# Create an STK environment.
stkbase = stk_env(envopts)

cbs_rcvd = 0
expired = 0

# Class containing callbacks for the name service
class name_service_cb(stk_callback):
	def __init__(self):
		stk_callback.__init__(self)
	def close(self):
		stk_callback.close(self)
	def print_meta_data(self,seq,data,user_type,clientd):
		if user_type == STK_MD_HTTPD_TCP_ID:
			print "HTTP monitoring TCP Connection " + data
		elif user_type == STK_MD_HTTPD_UDP_ID:
			print "HTTP monitoring UDP Connection " + data
		elif user_type == STK_MD_HTTPD_MCAST_ID:
			print "HTTP monitoring multicast Connection " + data
		else:
			print "Meta data type " + str(user_type) + " sz " + str(len(data))
	def name_info_cb(self,name_info,server_info,app_info,cb_type):
		global cbs_rcvd
		cbs_rcvd += 1
		if cb_type == STK_NS_REQUEST_EXPIRED:
			print "Request expired on name " + name_info.name()
			global expired
			expired = 1
			return
		ip = name_info.ip(0)
		if name_info.ft_state() == STK_NAME_ACTIVE:
			ft_state = "active"
		else:
			ft_state = "backup"
		print "Received info on name " + name_info.name() + ", IP " + ip.ipstr + " Port " + ip.portstr + " Protocol " + ip.protocol + " FT State " + ft_state
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
	def process_data(self,rcvchannel,rcv_seq):
		pass
	def process_name_response(self,rcvchannel,rcv_seq): # Callback to receive name info
		try:
			stk_name_service.invoke(rcv_seq)
		except Exception, e:
			print "Exception occurred processing received name response: " + str(e)

dispatcher_cbs = dispatcher_cb()

try:
	# Create name service callbacks object and request info on name
	name_info_cb = name_service_cb()

	if opts.subscribe:
		rc = stk_name_service.subscribe_to_name_info(stkbase,opts.name,name_info_cb,None,None)
	else:
		rc = stk_name_service.request_name_info(stkbase,opts.name,1000,name_info_cb,None,None)
	if rc != STK_SUCCESS:
		print "Failed to request name"
		cleanup()
		sys.exit(5);
except Exception, e:
	print "Exception occurred trying to request name info: " + str(e)
	cleanup()
	sys.exit(5)


# Run the client dispatcher to receive data until we get all the responses
while (opts.subscribe) or (cbs_rcvd < opts.callbacks and expired == 0):
	try:
		stkbase.client_dispatcher_timed(dispatcher_cbs,1000);
		print "Received " + str(cbs_rcvd) + " sequences, waiting for " + str(opts.callbacks)
	except Exception, e:
		print "Exception occurred waiting for data to arrive: " + str(e)

stkbase.terminate_dispatcher()

# The dispatcher returned, cleanup everything
cleanup()

if expired:
	print "name lookup of '" + opts.name + "' expired"
else:
	print "Received " + str(cbs_rcvd) + " name lookup callbacks for '" + opts.name + "'"

