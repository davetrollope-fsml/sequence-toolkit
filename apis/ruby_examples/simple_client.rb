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

require 'time'
require 'getoptlong'

# Sequence Toolkit modules
require 'stk'
require 'stk_name_service'
require 'stk_examples'
require 'stk_service_group'
require 'stk_tcp_server'
require 'stk_udp_client'
require 'stk_rawudp_client'
require 'stkdata_flow'

class Cmdopts
	attr_accessor :name_server_ip,:name_server_port,:name_server_protocol
	attr_accessor :monitor_ip,:monitor_port,:monitor_protocol
	attr_accessor :server_ip,:server_port
	attr_accessor :group_name
	attr_accessor :quiet
	attr_accessor :cbs
	attr_accessor :seqs,:callbacks,:service_name
	def initialize
		@server_port = 0
		@name_server_ip = "127.0.0.1"
		@name_server_port = "20002"
		@name_server_protocol = "tcp"
		@monitor_ip = "127.0.0.1"
		@monitor_port = "20001"
		@monitor_protocol = "tcp"
		@group_name = "Simple Server Service Group"
		@quiet = false
		@seqs = 100
		@callbacks = 2
		@cbs = 2
		@service_name = "simple_client service"
		@@opts = self
	end
	def self.opts
		@@opts
	end
end
@opts = Cmdopts.new

def usage()
	puts <<-EOF
simple_client.rb [OPTION] 
				  -h                        : This help!
				  -i lookup:<name>          : Name of server (name server will be used to get the ip/port)
				  -q                        : Quiet
				  -B ip[:port]              : IP and port to be bound (default: 0.0.0.0:29312)
				  -G <name>                 : Group Name for services
				  -s <sequences>            : # of sequences
				  -S <name>                 : Service Name
				  -m <[protocol:]ip[:port]> : IP and port of monitor")
				  -R <[protocol:]ip[:port]> : IP and port of name server
EOF
end

def process_cmdline()
	gopts = GetoptLong.new(
	  [ '--help', '-h', GetoptLong::NO_ARGUMENT ],
	  [ '--ip', '-i', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--quiet', '-q', GetoptLong::NO_ARGUMENT ],
	  [ '--group-name', '-G', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--name-server', '-R', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--bind', '-B', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--monitor', '-m', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--sequences', '-s', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--service-name', '-S', GetoptLong::REQUIRED_ARGUMENT ]
	)

	gopts.each do |opt, arg|
	  case opt
		when '--help'
			usage()
		when '--callbacks'
			@opts.cbs = arg.to_i
		when '--group-name'
			@opts.group_name = arg
		when '--service-name'
			@opts.service_name = arg
		when '--sequences'
			@opts.seqs = arg.to_i
		when '--quiet'
			@opts.quiet = true
		when '--bind'
			bind_ip = arg.split(':')
			@opts.bind_ip = bind_ip[0]
			@opts.bind_port = bind_ip[1] if bind_ip.length == 2
		when '--name-server'
			p=Stkenv::Stk_protocol_def_t.new
			Stkdata_flow::stk_data_flow_parse_protocol_str(p,arg)
			@opts.name_server_ip = p.ip if p.ip != ''
			@opts.name_server_port = p.port if p.port != ''
			@opts.name_server_protocol = p.protocol if p.protocol != ''
		when '--monitor'
			p=Stkenv::Stk_protocol_def_t.new
			Stkdata_flow::stk_data_flow_parse_protocol_str(p,arg)
			@opts.monitor_ip = p.ip if p.ip != ''
			@opts.monitor_port = p.port if p.port != ''
			@opts.monitor_protocol = p.protocol if p.protocol != ''
		end
	end
end
process_cmdline()

# This example batches up sequences to be processed for efficiency
BATCH_SIZE=5

# Create the STK environment - can't do anything without one
envopts_hash = {
	:name_server_options => {
		:name_server_data_flow_protocol => @opts.name_server_protocol,
		:name_server_data_flow_options => {
			:data_flow_name => 'name server socket for simple_server',
			:data_flow_id => 10000,
			:destination_address => @opts.name_server_ip,
			:destination_port => @opts.name_server_port
		}
	},
	:monitor_options => {
		:name_server_data_flow_protocol => @opts.monitor_protocol,
		:name_server_data_flow_options => {
			:data_flow_name => 'monitoring socket for simple_server',
			:data_flow_id => 10001,
			:destination_address => @opts.monitor_ip,
			:destination_port => @opts.monitor_port,
			:nodelay => 1
		}
	}
}
envopts = Stk_options.new(envopts_hash)

# Let the environment automatically add and remove fds for these data flows to the dispatcher
Stk_env.append_name_server_dispatcher_cbs(envopts,"name_server_data_flow")
Stk_env.append_monitoring_dispatcher_cbs(envopts,"monitoring_data_flow")

stkbase = Stk_env.new(envopts)

class Stats
	class << self
		attr_accessor :expired
		attr_accessor :cbs_rcvd
		attr_accessor :seqs_rcvd
	end
end
Stats.cbs_rcvd = 0
Stats.seqs_rcvd = 0
Stats.expired = false

class DataConnections
	@@connections = []
	def self.add(connection)
		@@connections << connection
	end
	def self.free_options
		@@connections.each do |connection|
			if !connection[:data_flow_options].nil?
				connection[:data_flow_options].remove_dispatcher_fd_cbs()
				connection[:data_flow_options].close()
			end
		end
	end
	def self.count
		@@connections.count
	end
	def self.connections
		@@connections
	end
end
class Name_service_cb < Stk_callback
	def print_meta_data(seq,data,user_type,clientd)
		case user_type
		when Stkname_service::STK_MD_HTTPD_TCP_ID
			puts "HTTP monitoring TCP Connection #{data}"
		when Stkname_service::STK_MD_HTTPD_UDP_ID
			puts "HTTP monitoring UDP Connection #{data}"
		when Stkname_service::STK_MD_HTTPD_MCAST_ID
			puts "HTTP monitoring multicast Connection #{data}"
		else
			puts "Meta data type #{user_type} sz #{data.length}"
		end
	end
	def name_info_cb(name_info,server_info,app_info,cb_type)
		Stats.cbs_rcvd += 1
		if cb_type == Stkname_service::STK_NS_REQUEST_EXPIRED
			puts "Request expired on name #{name_info.name()}"
			Stats.expired = true
			return
		end
		ip = name_info.ip(0)
		ft_state = name_info.ft_state() == Stkname_service::STK_NAME_ACTIVE ? "active" : "backup"
		puts "Received info on name #{name_info.name()}, IP #{ip.ipstr} Port #{ip.portstr} Protocol #{ip.protocol} FT State #{ft_state}"
		Cmdopts.opts.server_ip = ip.ipstr
		Cmdopts.opts.server_port = ip.portstr
		stkbase = @app_info
		meta_data = name_info.meta_data(stkbase)
		meta_data.iterate(nil) do |seq,data,user_type,clientd|
			print_meta_data seq,data,user_type,clientd
		end
	end
	def app_info=(app_info)
		@app_info = app_info
	end
end

class Dispatcher_cb < Stk_callback
	@@num ||= 0
	def process_seq_segment(seq,str,user_type,clientd)
		if Cmdopts.opts.quiet == false
			puts "Sequence #{seq.id()} Received #{str.length} bytes of type #{user_type}"
			if str.length >= 4
				data = str.bytes
				sz = data.count
				printf "Bytes: %02x %02x %02x %02x ... %02x %02x %02x %02x",
					data[0],data[1],data[2],data[3],
					data[sz - 4],data[sz - 3],data[sz - 2],data[sz - 1]
			end
		end
	end
	def process_data(rcvchannel,rcv_seq) # Callback to receive data
		begin
			# Call process_seq_segment() on each element in the sequence
			rcv_seq.iterate(nil) do |seq,data,user_type,clientd|
				process_seq_segment seq,data,user_type,clientd
			end

			Stats.seqs_rcvd += 1

			if Stats.seqs_rcvd == Cmdopts.opts.seqs or (Stats.seqs_rcvd % BATCH_SIZE == BATCH_SIZE - 1)
				rcvchannel.env.stop_dispatcher()
			end
		rescue Exception => e
			puts "#{e.inspect}"
		end
	end
	def process_name_response(rcvchannel,rcv_seq)
		begin
			Stk_name_service.invoke(rcv_seq)
		rescue Exception => e
			puts "Exception occurred processing received name response: #{e.inspect}"
		end
	end
	def fd_created(rcvchannel,fd)
		publisher = { :data_flow => rcvchannel, :publisher_fd => fd }
		DataConnections.add(publisher)
	end
	def fd_destroyed(rcvchannel,fd)
		DataConnections.connections.each do |connection|
			DataConnections.connections.delete(connection) if connection[:publisher_fd] == fd
		end
	end
end
name_service_cbs = Name_service_cb.new
dispatcher_cbs = Dispatcher_cb.new

Stk_examples::name_lookup_and_dispatch(stkbase,@opts.group_name,name_service_cbs,@opts,Stats,dispatcher_cbs,false)

Stats.seqs_rcvd = 0
df_opts_hash = {
	:destination_address => @opts.server_ip,
	:destination_port => @opts.server_port,
	:nodelay => 1,
}
df_opts = Stk_options.new(df_opts_hash)
df_opts.append_dispatcher_fd_cbs(nil)

# Create the TCP server
df = Stk_tcp_client.new(stkbase,"tcp client data flow for simple_client", 29090, df_opts)
if df.nil?
	puts "Failed to create the server data flow"
	cleanup()
	exit(5)
end

puts "Client data flow created"


svc_id = 0x5e671ce

svc_opts = Stk_options.new("")
svc_opts.append_data_flow("notification_data_flow",df)

begin
	svc = Stk_service.new(stkbase,@opts.service_name,svc_id,Stksequence::STK_SERVICE_TYPE_DATA,svc_opts)
rescue Exception => e
	puts "Failed to create the client service #{e}"
	exit 5
end

# Set this service to a running state so folks know we are in good shape
svc.set_state(Stkservice::STK_SERVICE_STATE_RUNNING)

seq = Stk_sequence.new(stkbase,"simple_client sequence",0xfedcba98,Stksequence::STK_SEQUENCE_TYPE_DATA,Stksequence::STK_SERVICE_TYPE_DATA,nil)

default_buffer = []
50.times do |i|
	default_buffer << i
end

seq.copy_array_to(default_buffer,0x135);
# Can also copy strings using copy_string_to
#seq.copy_string_to("string test",0x135);

# Send data over TCP
seqs_sent = 0
blks = 0
errors = 0

while seqs_sent < @opts.seqs do
	begin
		rc = df.send(seq,Stktcp_client::STK_TCP_SEND_FLAG_NONBLOCK)
		if rc == Stkdata_flow::STK_SUCCESS or rc == Stkdata_flow::STK_WOULDBLOCK
			if rc == Stkdata_flow::STK_SUCCESS
				seqs_sent += 1
				svc.update_smartbeat_checkpoint(seqs_sent)
			end
			if rc == Stkdata_flow::STK_WOULDBLOCK or seqs_sent % BATCH_SIZE == BATCH_SIZE - 1
				# Run the example client dispatcher to receive data for up to 10ms
				stkbase.client_dispatcher_timed(dispatcher_cbs,10)
			end
			time.sleep(0.001) if rc != Stkdata_flow::STK_SUCCESS
		else
			print "Failed to send data to remote service #{rc}" if opts.quiet == false
		end
	rescue Exception => e
		print "Exception occurred trying to send data: #{e.inspect}"
		errors += 1
		break if errors > 100
	end
end

print "Sent #{seqs_sent} sequences"


begin
	stkbase.client_dispatcher_timed(dispatcher_cbs,1000);
rescue Exception => e
	print "Exception occurred dispatching: #{e.inspect}"
end

# Free connections
puts "Closing data flow #{df.class.name}"
df.close()

# Get rid of the callbacks
name_service_cbs.close()
dispatcher_cbs.close()

# And get rid of the environment, we are done!
stkbase.close()

# Now free the options that were built
# Because there was nested options, we must free each nest individually
# because there is no stored indication which options are nested
envopts.free_built_options(envopts.find_sub_option("name_server_options"))
envopts.free_built_options(nil)
envopts.close()

