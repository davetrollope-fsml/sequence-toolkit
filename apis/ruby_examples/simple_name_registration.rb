
# This file implements a lookup of a name on the name server

require 'time'
require 'getoptlong'

# Sequence Toolkit modules
require 'stk'
require 'stk_name_service'

class Cmdopts
	attr_accessor :name
	attr_accessor :name_server_ip,:name_server_port,:name_server_protocol
	attr_accessor :server_ip,:server_port
	attr_accessor :cbs
	attr_accessor :group_name
	attr_accessor :linger
	attr_accessor :meta_data
	attr_accessor :ft_state
	def initialize
		@name_server_ip = "127.0.0.1"
		@name_server_port = "20002"
		@name_server_protocol = "tcp"
		@cbs = 2
		@linger = 300
		@meta_data = {}
		@ft_state = "active"
	end
end
@opts = Cmdopts.new

def usage()
	puts <<-EOF
simple_name_registration [OPTION] <name>... 
				  -i <ip[:port]>            : IP and port of server
				  -M <id,value>             : meta data (integer id, string value)
				  -F <active|backup>        : Fault tolerant state to register with name
				  -G <name>                 : Group Name for services
				  -L <linger sec>           : Time name should exist after connection to name server dies
				  -c #                      : Number of callbacks
				  -R <[protocol:]ip[:port]> : IP and port of name server
EOF
end

def process_cmdline()
	gopts = GetoptLong.new(
	  [ '--help', '-h', GetoptLong::NO_ARGUMENT ],
	  [ '--callbacks', '-c', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--group-name', '-G', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--linger', '-L', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--ip', '-i', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--ft-state', '-F', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--name-server', '-R', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--meta-data', '-M', GetoptLong::REQUIRED_ARGUMENT ]
	)

	gopts.each do |opt, arg|
	  case opt
		when '--help'
			usage()
		when '--callbacks'
			@opts.cbs = arg.to_i
		when '--linger'
			@opts.linger = arg.to_i
		when '--group-name'
			@opts.group_name = arg
		when '--ft-state'
			@opts.ft_state = arg
		when '--ip'
			server_ip = arg.split(':')
			@opts.server_ip = server_ip[0]
			@opts.server_port = server_ip[1] if server_ip.length == 2
		when '--meta-data'
			metadata = arg.split(':')
			@opts.meta_data[metadata[0].to_i] = metadata[1]
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
			:data_flow_name => 'name server socket for simple_name_registration',
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
		puts "Received info on name " + name_info.name() + ", IP " + ip.ipstr + " Port " + ip.portstr + " Protocol " + ip.protocol
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

meta_data_seq = Stk_sequence.new(stkbase,"meta data sequence",0,Stksequence::STK_SEQUENCE_TYPE_DATA,Stksequence::STK_SERVICE_TYPE_DATA,nil)
if meta_data_seq.nil?
	print "Failed to create the client sequence"
	exit(5)
end

@opts.meta_data.each do |k,v|
	puts "#{k} #{v}"
	meta_data_seq.copy_string_to(v,k)
end

# Register name
print "Registering info on name #{@opts.name}"
name_opts_hash = {
	:connect_address => @opts.server_ip,
	:connect_port => @opts.server_port,
	:fault_tolerant_state => @opts.ft_state
}
name_options = Stk_options.new(name_opts_hash)
name_options.append_sequence("meta_data_sequence",meta_data_seq)

begin
	Stk_name_service.register_name(stkbase,@opts.name,@opts.linger,1000,name_service_cbs,stkbase,name_options)
rescue Exception => e
	print "Exception occurred trying to register name: #{e.inspect}"
end

while Stats.cbs_rcvd < @opts.cbs && Stats.expired == false do
	begin
		stkbase.client_dispatcher_timed(dispatcher_cbs,1000);
		puts "Received #{Stats.cbs_rcvd} name registration callbacks, waiting for #{@opts.cbs}"
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

