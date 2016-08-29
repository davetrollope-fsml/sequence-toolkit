# Copyright Dave Trollope 2015
#
# This file implements a basic service and is intended to be a simple introduction
# to the STK for client type applications. It is intended to be used with simple_server.
#
# This example queries the name server for a service name, sets up service monitoring,
# creates a service and creates a TCP data flow to connect to the (simple_)server.
#
# It sets the service state to RUNNING and sends sequences to the server which reflects
# them back.
#
# Before closing, the service state is set to STOPPING and then the example exits.
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
	service_name = "simple_client service"
	quiet = False
	seqs = 100
	monitor_ip = "127.0.0.1"
	monitor_port = "20001"
	monitor_protocol = "tcp"
	name_server_ip = "127.0.0.1"
	name_server_port = "20002"
	name_server_protocol = "tcp"
	server_name = "Simple Server Service Group"
	server_ip = None
	server_port = 0

opts = cmdopts()

# Process command line options
def process_cmdline():
	try:
		gopts, args = getopt.getopt(sys.argv[1:], "hqG:S:s:i:m:R:", ["help", "quiet", "group-name=", "service-name="])
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
		elif o in ("-G", "--group-name"):
			opts.group_name = a
		elif o in ("-S", "--service-name"):
			opts.service_name = a
		elif o in ("-s"):
			opts.seqs = int(a)
		elif o in ("-i"):
			server_ip = a.split(':')
			if server_ip[0] != "lookup":
				print("Please use 'lookup:<name>' instead of " + server_ip[0])
				sys.exit(5);
			opts.server_name = server_ip[1]
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
	print("Usage: simple_client.py [options]")
	print("       -h                        : This help!")
	print("       -i lookup:<name>          : Name of server (name server will be used to get the ip/port)")
	print("                                 : default: 'Simple Server Service Group'")
	print("       -q                        : Quiet")
	print("       -G <name>                 : Group Name for services")
	print("       -s <sequences>            : # of sequences")
	print("       -S <name>                 : Service Name")
	print("       -m <[protocol:]ip[:port]> : IP and port of monitor")
	print("                                 : protocol may be <tcp|udp>")
	print("       -R <[protocol:]ip[:port]> : IP and port of name server")

# This example batches up sequences to be processed for efficiency
BATCH_SIZE=5

# Process command line options
process_cmdline()

df = None
df_opts = None
dispatcher_cbs = None
svc = None
svc_opts = None
seq = None
stkbase = None
envopts = None

def cleanup():
	try:
		# destroy the data flow, sequence, service group and environment
		if seq:
			seq.close()
		if svc:
			svc.close()
		if svc_opts:
			svc_opts.remove_data_flow("notification_data_flow")
			svc_opts.close()
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
			stk_env.remove_monitoring_dispatcher_cbs(envopts,"monitoring_data_flow")
			envopts.free_sub_option("monitoring_data_flow_options")
			envopts.free_sub_option("name_server_options")
			envopts.close()
	except Exception, e:
		print "Exception occurred during cleanup: " + str(e)

# Round off number of requested sequences to be a multiplier of the batch size
opts.seqs -= (opts.seqs % BATCH_SIZE)

# Create the STK environment - can't do anything without one
# By configuring the IP/Port of the monitoring service when we create the environment
# all services created within it will automatically use the monitoring service unless
# overridden.
opts_dict = {
	"name_server_options": {
		"name_server_data_flow_protocol": opts.name_server_protocol,
		"name_server_data_flow_options": {
			"data_flow_name": """%(protocol)s name server socket for simple_client""" % {"protocol": opts.name_server_protocol},
			"data_flow_id": 10000,
			"destination_address": opts.name_server_ip,
			"destination_port": opts.name_server_port
		}
	},
	"monitoring_data_flow_protocol": opts.monitor_protocol,
	"monitoring_data_flow_options": {
		"data_flow_name": """%(protocol)s monitoring socket for simple_client""" % {"protocol": opts.monitor_protocol},
		"data_flow_id": 10001,
		"destination_address": opts.monitor_ip,
		"destination_port": opts.monitor_port,
		"nodelay": 1
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
		opts.server_ip = ip.ipstr
		opts.server_port = ip.portstr
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

dispatcher_cbs = dispatcher_cb()

# Let the environment automatically add and remove fds for these data flows to the dispatcher
stk_env.append_name_server_dispatcher_cbs(envopts,"name_server_data_flow")
stk_env.append_monitoring_dispatcher_cbs(envopts,"monitoring_data_flow")

# Create an STK environment.
stkbase = stk_env(envopts)

# Create name service callbacks object and request info on name
name_info_cb = name_service_cb()

stk_examples.name_lookup_and_dispatch(stkbase,opts.server_name,name_info_cb,opts,data,dispatcher_cbs,False,cleanup)

seqs_rcvd = 0
# Create the options for the client data flow
print "Creating client data flow"
df_opts_dict = {
	"destination_address": opts.server_ip,
	"destination_port": opts.server_port,
	"nodelay": 1
}
df_opts = stk_options(df_opts_dict)
df_opts.append_dispatcher_fd_cbs(None)

# Create the TCP client data flow to the server
df = stk_tcp_client(stkbase,"tcp client socket for simple_client", 29090, df_opts)
if df == None:
	print "Failed to create the server data flow"
	cleanup()
	exit(5)

print "Client data flow created"

# Create our simple client service using a random ID, monitoring is inherited from the environment
random.seed()
svc_id = random.randint(0,0x7fffffffffffffff)
print "Using random service ID: " + str(svc_id)

svc_opts = stk_options("")
svc_opts.append_data_flow("notification_data_flow",df)

svc = stk_service(stkbase,opts.service_name,svc_id,STK_SERVICE_TYPE_DATA,svc_opts)
if svc == None:
	print "Failed to create the client service"
	cleanup()
	exit(5)

# Set this service to a running state so folks know we are in good shape
svc.set_state(STK_SERVICE_STATE_RUNNING)

seq = stk_sequence.stk_sequence(stkbase,"simple_client sequence",0xfedcba98,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,None)
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

# Send data over TCP
seqs_sent = 0
blks = 0
errors = 0

while seqs_sent < opts.seqs:
	try:
		rc = df.send(seq,STK_TCP_SEND_FLAG_NONBLOCK)
		if rc == STK_SUCCESS or rc == STK_WOULDBLOCK:
			if rc == STK_SUCCESS:
				seqs_sent += 1
				svc.update_smartbeat_checkpoint(seqs_sent)
			if rc == STK_WOULDBLOCK or seqs_sent % BATCH_SIZE == BATCH_SIZE - 1:
				# Run the example client dispatcher to receive data for up to 10ms
				stkbase.client_dispatcher_timed(dispatcher_cbs,10);
			if rc != STK_SUCCESS:
				time.sleep(0.001)
		else:
			if opts.quiet == False:
				print "Failed to send data to remote service " + str(rc)
	except Exception, e:
		print "Exception occurred trying to send data: " + str(e)
		errors += 1
		if errors > 100:
			break

print "Sent " + str(seqs_sent) + " sequences"

# Run the client dispatcher to receive data until we get all the responses
while seqs_rcvd < opts.seqs:
	try:
		print "received " + str(seqs_rcvd) + " sequences"
		stkbase.client_dispatcher_timed(dispatcher_cbs,1000);
	except Exception, e:
		print "Exception occurred waiting for data to arrive: " + str(e)

print "Done " + str(seqs_sent) + " sequences"

svc.set_state(STK_SERVICE_STATE_STOPPING)

print "Waiting 5 seconds before closing"
stkbase.client_dispatcher_timed(dispatcher_cbs,5000);

stkbase.terminate_dispatcher()

# The dispatcher returned, cleanup everything
cleanup()

