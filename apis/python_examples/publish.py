# Copyright Dave Trollope 2014
# This source code is not to be distributed without agreement from
# D. Trollope
#
# This file implements a basic publisher.
# It supports TCP and UDP (Raw, Unicast and Multicast) data flows.
#
# This example registers the publisher name with the name server and includes the data
# flow IP/Port. Subscribers use name subscriptions to receive the IP/Port info from the
# name server and join the advertised address.
#
# TCP Publishers are listening data flows and use the TCP Server API. UDP Publishers
# rely on the UDP Client API to publish.
#
# However, for UDP (Unicast) this implicitly dictates the IP and Port the subscriber needs
# to join on which, due to the nature of UDP prevents multiple subscribers joining the publisher.
# Unidirectional/directed flows like this are still useful because they decouple the application
# subscribing from needing to be configured with the IP/Port.
#
# This example uses several macros defined in stk_examples.h to simplify understanding and
# keep focus on the most important details.

# System imports
import getopt, sys, time, random

import stk_examples

# Sequence Toolkit imports - stk_env must be first
from stk_env import *
from stk_options import *
from stk_service_group import *
from stk_service import *
from stk_tcp_server import *
from stk_udp_client import *
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
	group_name = ""
	callbacks = 2
	quiet = False
	seqs = 100
	bind_ip = "127.0.0.1"
	bind_port = "29312"
	name_server_ip = "127.0.0.1"
	name_server_port = "20002"
	name_server_protocol = "tcp"
	publisher_ip = "127.0.0.1"
	publisher_port = "29312"
	publisher_protocol = "tcp"
	publisher_name = None
	protocol = 0
	linger = 5 # Keep names around for 5 seconds fter death

opts = cmdopts()

# Process command line options
def process_cmdline():
	try:
		gopts, args = getopt.getopt(sys.argv[1:], "hqG:B:L:S:s:i:m:R:", ["help", "quiet", "group-name=", "linger=", "service-name="])
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
		elif o in ("-i"):
			p=stk_protocol_def_t()
			a=stk_data_flow_parse_protocol_str(p,a)
			if p.ip != '':
				opts.publisher_ip = p.ip
			if p.port != '':
				opts.publisher_port = p.port
			if p.protocol != '':
				opts.publisher_protocol = p.protocol

			if opts.publisher_protocol == "tcp":
				opts.protocol = 0
			elif opts.publisher_protocol == "rawudp":
				opts.protocol = 1
			elif opts.publisher_protocol == "udp":
				opts.protocol = 2
			elif opts.publisher_protocol == "multicast":
				opts.protocol = 3
		elif o in ("-L", "--linger"):
			opts.linger = int(a)
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
		sys.exit()

	opts.publisher_name = args[0]

def usage():
	print("Usage: publish.py [options] name")
	print("       -h                        : This help!")
	print("       -i <[protocol:]ip[:port]> : IP and port of publisher (default: tcp:127.0.0.1:29312)")
	print("       -q                        : Quiet")
	print("       -B ip[:port]              : IP and port to be bound (default: 0.0.0.0:29312)")
	print("       -s <sequences>            : # of sequences")
	print("       -R <[protocol:]ip[:port]> : IP and port of name server")

# This example batches up sequences to be processed for efficiency
BATCH_SIZE=5

# Process command line options
process_cmdline()

df = None
df_opts = None
dispatcher_cbs = None
seq = None
stkbase = None
envopts = None
data_connections = []
seqs_rcvd = 0

def cleanup():
	try:
		# destroy the data flow, sequence and environment
		if seq:
			seq.close()
		if df:
			df.close()
		if df_opts:
			df_opts.remove_dispatcher_fd_cbs()
			df_opts.close()
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

# Round off number of requested sequences to be a multiplier of the batch size
opts.seqs -= (opts.seqs % BATCH_SIZE)

# Create the STK environment - can't do anything without one
opts_dict = {
	"name_server_options": {
		"name_server_data_flow_protocol": opts.name_server_protocol,
		"name_server_data_flow_options": {
			"data_flow_name": """%(protocol)s name server socket for simple_client""" % {"protocol": opts.name_server_protocol},
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

# Class containing callbacks for name service
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

			if seqs_rcvd == opts.seqs or (seqs_rcvd % BATCH_SIZE == BATCH_SIZE - 1):
				stkbase.stop_dispatcher()
		except Exception, e:
			print str(e)
	def process_name_response(self,rcvchannel,rcv_seq): # Callback to receive name info
		try:
			stk_name_service.invoke(rcv_seq)
		except Exception, e:
			print "Exception occurred processing received data: " + str(e)
	def fd_created(self,rcvchannel,fd):
		global data_connections
		subscription = { 'data_flow': rcvchannel, 'subscriber_fd': fd }
		data_connections.append(subscription)
	def fd_destroyed(self,rcvchannel,fd):
		global data_connections
		for h in data_connections:
			if "subscriber_fd" in h and h["subscriber_fd"] == fd:
				data_connections.remove(h)

dispatcher_cbs = dispatcher_cb()

# Let the environment automatically add and remove fds for these data flows to the dispatcher
stk_env.append_name_server_dispatcher_cbs(envopts,"name_server_data_flow")

# Create an STK environment.
stkbase = stk_env(envopts)

# Create the options for the client data flow
print "Creating publisher data flow"

if opts.protocol == 0:
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

	# Create the TCP Publisher
	df = stk_tcp_publisher(stkbase,"tcp publisher data flow", 29090, df_opts)
	if df == None:
		print "Failed to create the publisher data flow"
		cleanup()
		exit(5)
elif opts.protocol == 1:
	df_opts_dict = {
		"destination_address": opts.publisher_ip,
		"destination_port": opts.publisher_port,
		"nodelay": 1,
		"send_buffer_size": 800000,
		"receive_buffer_size": 16000000
	}

	df_opts = stk_options(df_opts_dict)
	df_opts.append_dispatcher_fd_cbs(None)

	# Create the UDP Publisher
	df = stk_rawudp_publisher(stkbase,"rawudp publisher data flow", 29090, df_opts)
	if df == None:
		print "Failed to create the publisher data flow"
		cleanup()
		exit(5)

	subscription = { 'data_flow': df }
	data_connections.append(subscription)
elif opts.protocol == 2 or opts.protocol == 3:
	if opts.protocol == 3 and opts.publisher_ip == "127.0.0.1":
		opts.publisher_ip = "224.10.10.20"

	df_opts_dict = {
		"destination_address": opts.publisher_ip,
		"destination_port": opts.publisher_port,
		"nodelay": 1,
		"send_buffer_size": 800000,
		"receive_buffer_size": 16000000
	}
	df_opts = stk_options(df_opts_dict)
	df_opts.append_dispatcher_fd_cbs(None)

	# Create the UDP Publisher
	df = stk_udp_publisher(stkbase,"udp publisher data flow", 29090, df_opts)
	if df == None:
		print "Failed to create the publisher data flow"
		cleanup()
		exit(5)

	subscription = { 'data_flow': df }
	data_connections.append(subscription)

# Register name
print "Registering info on name " + opts.publisher_name
name_opts_dict = {
	"destination_address": opts.publisher_ip,
	"destination_port": opts.publisher_port,
	"destination_protocol": opts.publisher_protocol,
}
name_options = stk_options(name_opts_dict)

try:
	# Create name service callbacks object and register name
	name_info_cb = name_service_cb()

	rc = stk_name_service.register_name(stkbase, opts.publisher_name, opts.linger, 1000, name_info_cb, None, name_options);
	if rc != STK_SUCCESS:
		print "Failed to request name"
		cleanup()
		sys.exit(5);
except Exception, e:
	print "Exception occurred trying to register name: " + str(e)
	cleanup()
	sys.exit(5)


print "Publishing to " + opts.publisher_name

seq = stk_sequence.stk_sequence(stkbase,"publish sequence",0xfedcba98,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,None)
if seq == None:
	print "Failed to create the client sequence"
	cleanup()
	exit(5)

default_buffer = []
i = 0
while i < 50:
	default_buffer.append(i)
	i += 1

seq.copy_array_to(default_buffer,0x135);
# Can also copy strings using copy_string_to
#seq.copy_string_to("string test",0x135);

# Wait for initial connection
while len(data_connections) == 0:
	stkbase.client_dispatcher_timed(dispatcher_cbs,10);

# Send data 
seqs_sent = 0
blks = 0
errors = 0

print "Sending " + str(opts.seqs) + " sequences"
while seqs_sent < opts.seqs:
	try:
		rc = STK_SUCCESS
		for s in data_connections:
			nrc = s['data_flow'].send(seq,STK_TCP_SEND_FLAG_NONBLOCK)
			if nrc != STK_SUCCESS:
				rc = nrc
		if rc == STK_SUCCESS or rc == STK_WOULDBLOCK:
			if rc == STK_SUCCESS:
				seqs_sent += 1
			if rc == STK_WOULDBLOCK or seqs_sent % BATCH_SIZE == BATCH_SIZE - 1:
				# Run the example client dispatcher to receive data for up to 10ms
				stkbase.client_dispatcher_timed(dispatcher_cbs,10);
			if rc != STK_SUCCESS:
				time.sleep(0.001)
		else:
			if opts.quiet == False:
				print "Failed to send data " + str(rc)
	except Exception, e:
		print "Exception occurred trying to send data: " + str(e)
		errors += 1
		if errors > 100:
			break

print "Done " + str(seqs_sent) + " sequences"

print "Waiting 5 seconds before closing"
stkbase.client_dispatcher_timed(dispatcher_cbs,5000);

stkbase.terminate_dispatcher()

# The dispatcher returned, cleanup everything
cleanup()

