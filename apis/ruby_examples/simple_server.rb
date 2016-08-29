# Copyright Dave Trollope 2014
# This source code is not to be distributed without agreement from
# D. Trollope
#
# This example demonstrates how to code up a simple sequence server
# which accepts connections using the TCP data flow module,
# manages a service group and responds to sequences
# from a client.
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
	attr_accessor :bind_ip,:bind_port
	attr_accessor :group_name
	attr_accessor :quiet
	def initialize
		@bind_ip = "127.0.0.1"
		@bind_port = "29312"
		@name_server_ip = "127.0.0.1"
		@name_server_port = "20002"
		@name_server_protocol = "tcp"
		@monitor_ip = "127.0.0.1"
		@monitor_port = "20001"
		@monitor_protocol = "tcp"
		@group_name = "Simple Server Service Group"
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
simple_server.rb [OPTION] 
				  -h                        : This help!
				  -i <[protocol:]ip[:port]> : IP and port of publisher (default: tcp:127.0.0.1:29312)")
				  -q                        : Quiet
				  -B ip[:port]              : IP and port to be bound (default: 0.0.0.0:29312)
				  -G <name>                 : Group Name for services
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
	  [ '--monitor', '-m', GetoptLong::REQUIRED_ARGUMENT ]
	)

	gopts.each do |opt, arg|
	  case opt
		when '--help'
			usage()
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

# Class containing callbacks for services (added, removed and changing state)
class Service_cb < Stk_callback
	def added_cb(svcgrp,svc,state) # Service added callback
		puts "Service '#{svc.name()}' added from service group '#{svcgrp.name()}' [state #{state}]"
	end
	def removed_cb(svcgrp,svc,state) # Service removed callback
		puts "Service '#{svc.name()}' removed from service group '#{svcgrp.name()}' [state #{state}]"
	end
	def state_change_cb(svc,old_state,new_state) # Service changing state callback
		old_state_str = svc.state_str(old_state);
		new_state_str = svc.state_str(new_state);
		puts "Service '#{svc.name()}' changed from state #{old_state_str} to #{new_state_str}"
	end
	def smartbeat_cb(svcgrp,svc,smartbeat)
		begin
			puts "Service '#{svc.name()}' group '#{svcgrp.name()}' smartbeat received, checkpoint #{smartbeat.checkpoint()}"
		rescue Exception => e
			puts "#{e.inspect}"
		end
	end
end

class Dispatcher_cb < Stk_callback
	@@num ||= 0
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
		rescue Exception => e
			puts "#{e.inspect}"
		end
		begin
			ret_seq = Stk_sequence.new(rcv_seq.env(),"simple_server_return_data",0x7edcba90,Stksequence::STK_SEQUENCE_TYPE_DATA,Stksequence::STK_SERVICE_TYPE_DATA,nil)

			retbuf = []
			10.times do |i|
				retbuf << i
			end

			ret_seq.copy_array_to(retbuf,@@num);
			@@num = @@num + 1

			rcvchannel.send(ret_seq,Stktcp_client::STK_TCP_SEND_FLAG_NONBLOCK)
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

begin
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

	# Create the TCP server
	df = Stk_tcp_server.new(stkbase,"tcp server data flow", 29090, df_opts)
	if df.nil?
		puts "Failed to create the server data flow"
		cleanup()
		exit(5)
	end
rescue Exception => e
	puts "Exception occurred trying to create server data flow: #{e.inspect}"
end

puts "Server data flow created"

begin
	service_cbs = Service_cb.new

	svcgrp_opts = Stk_options.new("")
	svcgrp_opts.append_data_flow("listening_data_flow",df)
	svccb = Stk_service_group.add_service_cb_option(stkbase,svcgrp_opts,service_cbs)

	# Create the service group that client services will be added to as they are discovered.
	# Also, register callbacks so we can be notified when services are added and removed.
	svcgrp = Stk_service_group.new(stkbase, @opts.group_name, 1000, svcgrp_opts)
rescue Exception => e
	print "Exception occurred trying to create service group: #{e}"
end

begin
	# Run the example listening dispatcher to accept data flows from clients
	# and receive data from them. This example does this inline, but an
	# application might choose to invoke this on another thread.
	# 
	# The dispatcher only returns when a shutdown is detected.
	dispatcher_cbs = Dispatcher_cb.new
	stkbase.listening_dispatcher(df,svcgrp,dispatcher_cbs)
rescue Exception => e
	print "Exception occurred dispatching: #{e.inspect}"
end

# Free connections
puts "Closing data flow #{df.class.name}"
df.close()

# Get rid of the callbacks
name_service_cbs.close()
dispatcher_cbs.close()
service_cbs.close()

# And get rid of the environment, we are done!
stkbase.close()

# Now free the options that were built
# Because there was nested options, we must free each nest individually
# because there is no stored indication which options are nested
envopts.free_built_options(envopts.find_sub_option("name_server_options"))
envopts.free_built_options(nil)
envopts.close()

