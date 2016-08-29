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
# This example uses several methods defined in stk_examples.py to simplify understanding and
# keep focus on the most important details.

require 'time'
require 'getoptlong'

# Sequence Toolkit modules
require 'stk'
require 'stk_name_service'
require 'stk_examples'
require 'stk_tcp_server'
require 'stk_udp_client'
require 'stk_rawudp_client'
require 'stkdata_flow'

class Cmdopts
	attr_accessor :publisher_name
	attr_accessor :name_server_ip,:name_server_port,:name_server_protocol
	attr_accessor :publisher_ip,:publisher_port,:publisher_protocol
	attr_accessor :protocol
	attr_accessor :bind_ip,:bind_port
	attr_accessor :cbs,:seqs
	attr_accessor :group_name
	attr_accessor :quiet
	attr_accessor :linger
	def initialize
		@bind_ip = "127.0.0.1"
		@bind_port = "29312"
		@name_server_ip = "127.0.0.1"
		@name_server_port = "20002"
		@name_server_protocol = "tcp"
		@cbs = 2
		@seqs = 100
		@group_name = nil
		@quiet = false
		@linger = 5
		@publisher_ip = "127.0.0.1"
		@publisher_port = "29312"
		@publisher_protocol = "tcp"
		@protocol = 0
		@@opts = self
	end
	def self.opts
		@@opts
	end
end
@opts = Cmdopts.new

def usage()
	puts <<-EOF
publish [OPTION] <name>... 
				  -h                        : This help!
				  -i <[protocol:]ip[:port]> : IP and port of publisher (default: tcp:127.0.0.1:29312)")
				  -q                        : Quiet
				  -B ip[:port]              : IP and port to be bound (default: 0.0.0.0:29312)
				  -s <sequences>            : # of sequences
				  -G <name>                 : Group Name for services
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
	  [ '--sequences', '-S', GetoptLong::REQUIRED_ARGUMENT ]
	)

	gopts.each do |opt, arg|
	  case opt
		when '--help'
			usage()
		when '--sequences'
			@opts.seqs = arg.to_i
		when '--callbacks'
			@opts.cbs = arg.to_i
		when '--group-name'
			@opts.group_name = arg
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
		when '--ip'
			p=Stkenv::Stk_protocol_def_t.new
			Stkdata_flow::stk_data_flow_parse_protocol_str(p,arg)
			@opts.publisher_ip = p.ip if p.ip != ''
			@opts.publisher_port = p.port if p.port != ''
			@opts.publisher_protocol = p.protocol if p.protocol != ''
			case @opts.publisher_protocol
			when "tcp"
				@opts.protocol = 0
			when "rawudp"
				@opts.protocol = 1
			when "udp"
				@opts.protocol = 2
			when "multicast"
				@opts.protocol = 3
			end
		end
	end
	@opts.publisher_name = ARGV.shift
end
process_cmdline()

# This example batches up sequences to be processed for efficiency
BATCH_SIZE=5

puts "Name being looked up: " + @opts.publisher_name

# Create the STK environment - can't do anything without one
envopts_hash = {
	:name_server_options => {
		:name_server_data_flow_protocol => @opts.name_server_protocol,
		:name_server_data_flow_options => {
			:data_flow_name => 'name server socket for publish',
			:data_flow_id => 10000,
			:destination_address => @opts.name_server_ip,
			:destination_port => @opts.name_server_port
		}
	}
}
envopts = Stk_options.new(envopts_hash)

# Let the environment automatically add and remove fds for these data flows to the dispatcher
Stk_env.append_name_server_dispatcher_cbs(envopts,"name_server_data_flow")

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
	def process_seq_segment(seq,data,user_type,clientd)
		if Cmdopts.opts.quiet == false
			puts "Sequence #{seq.id()} Received #{data.length} bytes of type #{user_type}"
			#if data.length >= 4
				#sz = data.length
				#printf 'Bytes: %02x %02x %02x %02x ... %02x %02x %02x %02x',
					#data[0],data[1],data[2],data[3],
					#data[sz - 4],data[sz - 3],data[sz - 2],data[sz - 1]
			#end
		end
	end
	def process_data(rcvchannel,rcv_seq) # Callback to receive data
		begin
			# Call process_seq_segment() on each element in the sequence
			rcv_seq.iterate(nil) do |seq,data,user_type,clientd|
				process_seq_segment seq,data,user_type,clientd
			end

			Stats.seqs_rcvd += 1

			rcvchannel.env.stop_dispatcher() if Stats.seqs_rcvd == Cmdopts.opts.seqs
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
dispatcher_cbs = Dispatcher_cb.new
name_service_cbs = Name_service_cb.new

# Create the options for the client data flow
puts "Creating publisher data flow"

case @opts.protocol
when 0
	df_opts_hash = {
		:bind_address => @opts.bind_ip,
		:bind_port => @opts.bind_port,
		:nodelay => 1,
		:send_buffer_size => 800000,
		:receive_buffer_size => 16000000,
		:reuseaddr => 1
	}
	df_opts = Stk_options.new(df_opts_hash)
	df_opts.append_dispatcher_fd_cbs(nil)

	# Create the TCP Publisher
	df = Stk_tcp_publisher.new(stkbase,"tcp publisher data flow", 29090, df_opts)
	if df.nil?
		puts "Failed to create the publisher data flow"
		cleanup()
		exit(5)
	end
when 1
	df_opts_hash = {
		:destination_address => @opts.bind_ip,
		:destination_port => @opts.bind_port,
		:nodelay => 1,
		:send_buffer_size => 800000,
		:receive_buffer_size => 16000000
	}
	df_opts = Stk_options.new(df_opts_hash)
	df_opts.append_dispatcher_fd_cbs(nil)

	# Create the UDP Publisher
	df = Stk_rawudp_publisher.new(stkbase,"rawudp publisher data flow", 29090, df_opts)
	if df.nil?
		puts "Failed to create the publisher data flow"
		cleanup()
		exit(5)
	end

	publisher = { :data_flow => df }
	DataConnections.add(publisher)
when 2, 3
	if @opts.protocol == 3 && @opts.publisher_ip == "127.0.0.1"
		@opts.publisher_ip = "224.10.10.20"
	end

	df_opts_hash = {
		:destination_address => @opts.bind_ip,
		:destination_port => @opts.bind_port,
		:nodelay => 1,
		:send_buffer_size => 800000,
		:receive_buffer_size => 16000000
	}
	df_opts = Stk_options.new(df_opts_hash)
	df_opts.append_dispatcher_fd_cbs(nil)

	# Create the UDP Publisher
	df = Stk_udp_publisher.new(stkbase,"udp publisher data flow", 29090, df_opts)
	if df.nil?
		puts "Failed to create the publisher data flow"
		cleanup()
		exit(5)
	end

	publisher = { :data_flow => df }
	DataConnections.add(publisher)
else
	puts "Unrecognized protocol #{@opts.protocol}"
	return
end

puts "Publisher data flow created"

puts "Registering info on name #{@opts.publisher_name}"
name_options_hash = {
	:destination_address => @opts.publisher_ip,
	:destination_port => @opts.publisher_port,
	:destination_protocol => @opts.publisher_protocol
}
name_options = Stk_options.new(name_options_hash)

begin
	Stk_name_service.register_name(stkbase,@opts.publisher_name,@opts.linger,1000,name_service_cbs,stkbase,name_options)
rescue Exception => e
	puts "Exception occurred trying to register name: #{e.inspect}"
end

# Wait for responses for up to 1 second
stkbase.client_dispatcher_timed(dispatcher_cbs,1000);

seq = Stk_sequence.new(stkbase,"publish sequence",0xfedcba98,Stksequence::STK_SEQUENCE_TYPE_DATA,Stksequence::STK_SERVICE_TYPE_DATA,nil)
if seq.nil?
	puts "Failed to create the client sequence"
	cleanup()
	exit(5)
end

default_buffer = []
i = 0
while i < 50 do
	default_buffer << i
	i += 1
end

seq.copy_array_to(default_buffer,0x135);
# Can also copy strings using copy_string_to
#seq.copy_string_to("string test",0x135);

# Send data 
seqs_sent = 0
blks = 0
errors = 0

puts "Sending #{@opts.seqs} sequences"
while seqs_sent < @opts.seqs do
	# Wait for initial connection
	if DataConnections.count == 0
		puts "Sent #{seqs_sent} sequences, waiting for subscribers"
		while DataConnections.count == 0 do
			stkbase.client_dispatcher_timed(dispatcher_cbs,10);
		end
	end

	begin
		rc = Stkdata_flow::STK_SUCCESS
		DataConnections.connections.each do |s|
			nrc = s[:data_flow].send(seq,Stktcp_server::STK_TCP_SEND_FLAG_NONBLOCK)
			rc = nrc if nrc != Stkdata_flow::STK_SUCCESS
		end
		if rc == Stkdata_flow::STK_SUCCESS or rc == Stkdata_flow::STK_WOULDBLOCK
			seqs_sent += 1 if rc == Stkdata_flow::STK_SUCCESS
			if rc == Stkdata_flow::STK_WOULDBLOCK || seqs_sent % BATCH_SIZE == BATCH_SIZE - 1
				# Run the example client dispatcher to receive data for up to 10ms
				stkbase.client_dispatcher_timed(dispatcher_cbs,10)
			end
			time.sleep(0.001) if rc != Stkdata_flow::STK_SUCCESS
		else
			puts "Failed to send data #{rc}" if @opts.quiet == false
		end
	rescue Exception => e
		puts "Exception occurred trying to send data: #{e.inspect}"
		errors += 1
		break if errors > 100
	end
end

puts "Done #{seqs_sent} sequences"

puts "Waiting 5 seconds before closing"
stkbase.client_dispatcher_timed(dispatcher_cbs,5000);

# Free connections
puts "Closing data flow #{df.class.name}"
df.close()
DataConnections.free_options()

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

