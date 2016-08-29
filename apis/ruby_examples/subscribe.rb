
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

require 'time'
require 'getoptlong'

# Sequence Toolkit modules
require 'stk'
require 'stk_name_service'
require 'stk_examples'
require 'stk_options'
require 'stk_tcp_client'
require 'stk_udp_listener'
require 'stk_rawudp_listener'

class Cmdopts
	attr_accessor :subscriber_name
	attr_accessor :name_server_ip,:name_server_port,:name_server_protocol
	attr_accessor :bind_ip,:bind_port
	attr_accessor :cbs,:seqs
	attr_accessor :group_name
	attr_accessor :quiet
	def initialize
		@name_server_ip = "127.0.0.1"
		@name_server_port = "20002"
		@name_server_protocol = "tcp"
		@cbs = 2
		@seqs = 100
		@group_name = nil
		@quiet = false
		@@opts = self
	end
	def self.opts
		@@opts
	end
end
@opts = Cmdopts.new

def usage()
	puts <<-EOF
subscribe [OPTION] <name>... 
				  -h                        : This help!
				  -q                        : Quiet
				  -B ip[:port]              : IP and port to be bound (default: 0.0.0.0:29312)
				  -s <sequences>            : # of sequences
				  -c #                      : Number of callbacks
				  -G <name>                 : Group Name for services
				  -R <[protocol:]ip[:port]> : IP and port of name server
EOF
end

def process_cmdline()
	gopts = GetoptLong.new(
	  [ '--help', '-h', GetoptLong::NO_ARGUMENT ],
	  [ '--quiet', '-q', GetoptLong::NO_ARGUMENT ],
	  [ '--callbacks', '-c', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--group-name', '-G', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--name-server', '-R', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--bind', '-B', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--sequences', '-s', GetoptLong::REQUIRED_ARGUMENT ]
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
		end
	end
	@opts.subscriber_name = ARGV.shift
end
process_cmdline()

puts "Name being looked up: " + @opts.subscriber_name

# Create the STK environment - can't do anything without one
opts_hash = {
	:name_server_options => {
		:name_server_data_flow_protocol => @opts.name_server_protocol,
		:name_server_data_flow_options => {
			:data_flow_name => 'name server socket for subscribe',
			:data_flow_id => 10000,
			:destination_address => @opts.name_server_ip,
			:destination_port => @opts.name_server_port
		}
	}
}
opts_hash[:name_server_options].merge!({:group_name => @opts.group_name}) if @opts.group_name
envopts = Stk_options.new(opts_hash)

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
	def self.free
		@@connections.each do |connection|
			connection[:data_flow].close() if !connection[:data_flow].nil?
			if !connection[:data_flow_options].nil?
				connection[:data_flow_options].remove_dispatcher_fd_cbs()
				connection[:data_flow_options].close()
			end
		end
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
		begin
			create_data_flow(stkbase,ip.ipstr,ip.portstr,ip.protocol)
		rescue Exception => e
			puts "#{e.inspect}"
		end
	end
	def create_data_flow(stkbase,ip,port,protocol)
		# Create the options for the client data flow
		puts "Creating subscriber data flow"

		if protocol == "udp" or protocol == "rawudp" or protocol == "multicast"
			port = "29312" if port.nil?
			if protocol == "multicast"
				if @opts.bind_ip
					bind_ip = @opts.bind_ip
				else
					bind_ip = "0.0.0.0"
				end
			else
				bind_ip = ip
			end

			df_opts_hash = {
				:bind_address => bind_ip,
				:bind_port => port,
				:receive_buffer_size => 16000000,
				:reuseaddr => 1
			}
			df_opts_hash.merge!({:multicast_address => ip}) if protocol == "multicast"

			df_opts = Stk_options.new(df_opts_hash)
			df_opts.append_dispatcher_fd_cbs(nil)
			if protocol == "udp" or protocol == "multicast"
				df = Stk_udp_subscriber.new(stkbase,"udp subscriber data flow", 29090, df_opts)
			else
				df = Stk_rawudp_subscriber.new(stkbase,"rawudp subscriber data flow", 29090, df_opts)
			end
			if df.nil?
				print "Failed to create udp/rawudp subscriber data flow"
				cleanup()
				exit(5)
			end
		else
			if protocol == "tcp"
				df_opts_hash = {
					:destination_address => ip,
					:destination_port => port,
					:receive_buffer_size => 16000000,
					:nodelay => 1
				}
				df_opts = Stk_options.new(df_opts_hash)
				df_opts.append_dispatcher_fd_cbs(nil)
				# Create the TCP client data flow to the server
				df = Stk_tcp_subscriber.new(stkbase,"tcp subscriber data flow", 29090, df_opts)
				if df.nil?
					print "Failed to create the subscriber data flow"
					cleanup()
					exit(5)
				end
			else
				puts "Unrecognized protocol #{protocol}"
				return
			end
		end

		print "Subscriber data flow created"
		subscription = { :subscription_ip => ip, :subscription_port => port, :data_flow => df, :data_flow_options => df_opts }
		DataConnections.add(subscription)
	end
	def app_info=(app_info)
		@app_info = app_info
	end
end

class Dispatcher_cb < Stk_callback
	def process_seq_segment(seq,str,user_type,clientd)
		if Cmdopts.opts.quiet == false
			puts "Sequence #{seq.id()} Received #{str.length} bytes of type #{user_type}"
			if str.length >= 4
				data = str.bytes
				sz = data.count
				printf "Bytes: %02x %02x %02x %02x ... %02x %02x %02x %02x\n",
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
end
dispatcher_cbs = Dispatcher_cb.new
name_service_cbs = Name_service_cb.new

Stk_examples::name_lookup_and_dispatch(stkbase,@opts.subscriber_name,name_service_cbs,@opts,Stats,dispatcher_cbs,true)

while Stats.seqs_rcvd < @opts.seqs do
	begin
		stkbase.client_dispatcher_timed(dispatcher_cbs,1000);
		puts "Received #{Stats.seqs_rcvd} sequences"
	rescue StandardError => e
		puts "Exception occurred waiting for data to arrive: #{e.inspect}"
	end
end

# Free connections
DataConnections.free()

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

