# Copyright Dave Trollope 2015
# This source code is not to be distributed without agreement from
# D. Trollope
#
# This example demonstrates a basic subscriber which receives data
# from a publisher. It supports TCP and UDP (Raw, Unicast and Multicast) data flows.
#
# It creates a name subscription on the name server to subscribe to name registrations
# which contain the connection info required to listen to the data flow. The name subscription
# is maintained so multiple publishers can be joined, or if they restart.
#
# The publisher registers the name and connectivity info.
#
# This example uses several methods defined in stk_examples.py to simplify understanding and
# keep focus on the most important details.

# System imports
import getopt, sys, time, random

import stk_examples

# Sequence Toolkit imports - stk_env must be first
from stk_env import *
from stk_options import *
from stk_service_group import *
from stk_service import *
from stk_tcp_client import *
from stk_udp_listener import *
from stk_rawudp import *
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
	callbacks = 2
	quiet = False
	seqs = 100
	name_server_ip = "127.0.0.1"
	name_server_port = "20002"
	name_server_protocol = "tcp"
	subscriber_name = None
	server_ip = None
	server_port = 0
	bind_ip = None

opts = cmdopts()

# Process command line options
def process_cmdline():
	try:
		gopts, args = getopt.getopt(sys.argv[1:], "hqB:s:R:", ["help", "quiet"])
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
		elif o in ("-s"):
			opts.seqs = int(a)
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
		usage()
		sys.exit(5)

	opts.subscriber_name = args[0]


def usage():
	print("Usage: subscribe.py [options] name")
	print("       -h                        : This help!")
	print("       -q                        : Quiet")
	print("       -B ip[:port]              : IP and port to be bound (default: 0.0.0.0:29312)")
	print("       -s <sequences>            : # of sequences")
	print("       -R <[protocol:]ip[:port]> : IP and port of name server")

# Process command line options
process_cmdline()

df_opts = None
dispatcher_cbs = None
svc = None
svc_opts = None
seq = None
stkbase = None
envopts = None
data_connections=[]
seqs_rcvd = 0

def cleanup():
	try:
		# destroy the data flow, sequence, service group and environment
		for h in data_connections:
			if h['data_flow']:
				h['data_flow'].close()
			if h['data_flow_options']:
				h['data_flow_options'].remove_dispatcher_fd_cbs()
				h['data_flow_options'].close()
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
			dfopts = nsopts.find_option("name_server_data_flow_options")
			dfopts.remove_dispatcher_fd_cbs()
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
			"data_flow_name": """%(protocol)s name server socket for subscribe""" % {"protocol": opts.name_server_protocol},
			"data_flow_id": 10000,
			"destination_address": opts.name_server_ip,
			"destination_port": opts.name_server_port
		}
	}
}
envopts = stk_options(opts_dict)

class data:
	cbs_rcvd = 0
	expired = 0

# Class containing callbacks for services (added, removed and changing state)
class name_service_cb(stk_callback):
	def __init__(self):
		stk_callback.__init__(self)
	def close(self):
		stk_callback.close(self)
	def print_meta_data(self,seq,data,user_type,clientd):
		print "Meta data type " + str(user_type) + " sz " + str(len(data))
	def name_info_cb(self,name_info,server_info,app_info,cb_type):
		data.cbs_rcvd += 1
		if cb_type == STK_NS_REQUEST_EXPIRED:
			print "Request expired on name " + name_info.name()
			data.expired = 1
			return
		ip = name_info.ip(0)
		print "Received info on name " + name_info.name() + ", IP " + ip.ipstr + " Port " + ip.portstr + " Protocol " + ip.protocol
		global stkbase
		meta_data = name_info.meta_data(stkbase)
		if meta_data != None:
			meta_data.iterate(self.print_meta_data,None)
		try:
			self.create_data_flow(stkbase,ip.ipstr,ip.portstr,ip.protocol)
		except Exception, e:
			print str(e)
	def create_data_flow(self,stkbase,ip,port,protocol):
		# Create the options for the client data flow
		print "Creating subscriber data flow"

		if protocol == "udp" or protocol == "rawudp" or protocol == "multicast":
			if port == None:
				port = "29312"
			if protocol == "multicast":
				if opts.bind_ip:
					bind_ip = opts.bind_ip
				else:
					bind_ip = "0.0.0.0"
			else:
				bind_ip = ip

			df_opts_dict = {
				"bind_address": bind_ip,
				"bind_port": port,
				"receive_buffer_size": 16000000,
				"reuseaddr": 1
			}

			if protocol == "multicast":
				df_opts_dict["multicast_address"] = ip

			df_opts = stk_options(df_opts_dict)
			df_opts.append_dispatcher_fd_cbs(None)
			if protocol == "udp" or protocol == "multicast":
				df = stk_udp_subscriber(stkbase,"udp subscriber data flow", 29090, df_opts)
			else:
				df = stk_rawudp_subscriber(stkbase,"rawudp subscriber data flow", 29090, df_opts)
			if df == None:
				print "Failed to create udp/rawudp subscriber data flow"
				cleanup()
				exit(5)
		elif protocol == "tcp":
			df_opts_dict = {
				"destination_address": ip,
				"destination_port": port,
				"receive_buffer_size": 16000000,
				"nodelay": 1
			}
			df_opts = stk_options(df_opts_dict)
			df_opts.append_dispatcher_fd_cbs(None)
			# Create the TCP client data flow to the server
			df = stk_tcp_subscriber(stkbase,"tcp subscriber data flow", 29090, df_opts)
			if df == None:
				print "Failed to create the subscriber data flow"
				cleanup()
				exit(5)
		else:
			print "Unrecognized protocol " + protocol
			return

		print "Subscriber data flow created"
		global data_connections;
		subscription = { 'subscription_ip': ip, 'subscription_port': port, 'data_flow': df, 'data_flow_options': df_opts }
		data_connections.append(subscription)


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
	def process_data(self,rcvchannel,rcv_seq): # Callback to receive data
		try:
			global seqs_rcvd

			# Call process_seq_segment() on each element in the sequence
			rcv_seq.iterate(self.process_seq_segment,None)

			seqs_rcvd += 1

			if seqs_rcvd == opts.seqs:
				stkbase.stop_dispatcher()
		except Exception, e:
			print str(e)
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

# Create name service callbacks object and request info on name
name_info_cb = name_service_cb()

stk_examples.name_lookup_and_dispatch(stkbase,opts.subscriber_name,name_info_cb,opts,data,dispatcher_cbs,False,cleanup)

# Run the client dispatcher to receive data until we get all the responses
while seqs_rcvd < opts.seqs:
	try:
		print "received " + str(seqs_rcvd) + " sequences"
		stkbase.client_dispatcher_timed(dispatcher_cbs,1000);
	except Exception, e:
		print "Exception occurred waiting for data to arrive: " + str(e)

print "Done " + str(seqs_rcvd) + " sequences"

print "Waiting 5 seconds before closing"
stkbase.client_dispatcher_timed(dispatcher_cbs,5000);

stkbase.terminate_dispatcher()

# The dispatcher returned, cleanup everything
cleanup()

