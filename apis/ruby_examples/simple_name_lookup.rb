
# This file implements a lookup of a name on the name server

require 'time'
require 'getoptlong'

# Sequence Toolkit modules
require 'stk'
require 'stk_name_service'

class Cmdopts
	attr_accessor :name
	attr_accessor :subscribe
	attr_accessor :name_server_ip,:name_server_port,:name_server_protocol
	attr_accessor :cbs
	attr_accessor :group_name
	def initialize
		@subscribe = false
		@name_server_ip = "127.0.0.1"
		@name_server_port = "20002"
		@name_server_protocol = "tcp"
		@cbs = 2
		@group_name = nil
	end
end
@opts = Cmdopts.new

def usage()
	puts <<-EOF
simple_name_lookup [OPTION] <name>... 
				  -c #                      : Number of callbacks
				  -G <name>                 : Group Name for services
				  -R <[protocol:]ip[:port]> : IP and port of name server
				  -X                        : Subscribe mode
EOF
end

def process_cmdline()
	gopts = GetoptLong.new(
	  [ '--help', '-h', GetoptLong::NO_ARGUMENT ],
	  [ '--callbacks', '-c', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--group-name', '-G', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--subscribe', '-X', GetoptLong::NO_ARGUMENT ],
	  [ '--name-server', '-R', GetoptLong::REQUIRED_ARGUMENT ]
	)

	gopts.each do |opt, arg|
	  case opt
		when '--help'
			usage()
		when '--subscribe'
			@opts.subscribe = true
		when '--callbacks'
			@opts.cbs = arg.to_i
		when '--group-name'
			@opts.group_name = arg
		when '--name-server'
			p=Stkenv::Stk_protocol_def_t.new
			Stkdata_flow::stk_data_flow_parse_protocol_str(p,arg)
			@opts.name_server_ip = p.ip if p.ip != ''
			@opts.name_server_port = p.port if p.port != ''
			@opts.name_server_protocol = p.protocol if p.protocol != ''
		end
	end
	@opts.name = ARGV.shift
end
process_cmdline()

puts "Name being looked up: " + @opts.name

# Create the STK environment - can't do anything without one
opts_hash = {
	:name_server_options => {
		:name_server_data_flow_protocol => @opts.name_server_protocol,
		:name_server_data_flow_options => {
			:data_flow_name => 'name server socket for simple_name_lookup',
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
	end
end
Stats.cbs_rcvd = 0
Stats.expired = false

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
			puts "Request expired on name " + name_info.name()
			Stats.expired = true
			return
		end
		ip = name_info.ip(0)
		ft_state = name_info.ft_state() == Stkname_service::STK_NAME_ACTIVE ? "active" : "backup"
		puts "Received info on name " + name_info.name() + ", IP " + ip.ipstr + " Port " + ip.portstr + " Protocol " + ip.protocol + " FT State " + ft_state
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
	def process_data(rcvchannel,rcv_seq)
		puts "Dispatcher_cb#process_data"
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

if @opts.subscribe
	Stk_name_service.subscribe_to_name_info(stkbase,@opts.name,name_service_cbs,stkbase,nil)
else
	Stk_name_service.request_name_info(stkbase,@opts.name,1000,name_service_cbs,stkbase,nil)
end

while @opts.subscribe || (Stats.cbs_rcvd < @opts.cbs && Stats.expired == false) do
	begin
		stkbase.client_dispatcher_timed(dispatcher_cbs,1000);
		puts "Received #{Stats.cbs_rcvd} sequences, waiting for #{@opts.cbs}"
	rescue StandardError => e
		puts "Exception occurred waiting for data to arrive: #{e.inspect}"
	end
end

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

