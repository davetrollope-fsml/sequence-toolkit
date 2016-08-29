
# This file implements a basic service for monitoring purposes only and is intended to be
# a simple demonstration of how STK can be added to existing applications for monitoring.

require 'time'
require 'getoptlong'

# Sequence Toolkit modules
require 'stk'
require 'stk_service'
require 'stkservice'

class Cmdopts
	attr_accessor :service_name
	attr_accessor :end_threshold
 
	def initialize
		@service_name = "basic ruby monitored service"
		@end_threshold = 1000000
	end
end
@opts = Cmdopts.new

def usage()
	puts <<-EOF
monitored_service [OPTION] ... 
				  -S <service-name>
				  -t <end threshold>
EOF
end

def process_cmdline()
	gopts = GetoptLong.new(
	  [ '--help', '-h', GetoptLong::NO_ARGUMENT ],
	  [ '--service-name', '-S', GetoptLong::REQUIRED_ARGUMENT ],
	  [ '--end-threshold', '-t', GetoptLong::REQUIRED_ARGUMENT ]
	)

	gopts.each do |opt, arg|
	  case opt
		when '--help'
			usage()
		when '--end-threshold'
			@opts.end_threshold = arg.to_i
		when '--service-name'
			@opts.service_name = arg
		end
	end
end
process_cmdline()

# Create the STK environment - can't do anything without one
# By configuring the IP/Port of the monitoring service when we create the environment
# all services created within it will automatically use the monitoring service unless
# overridden.
opts_hash = {
	:name_server_options => {
		:data_flow_name => 'tcp name server socket for simple_client',
		:data_flow_id => 10000,
		:connect_address => '127.0.0.1',
		:connect_port => 20002
	},
	:monitoring_data_flow_options => {
		:data_flow_name => 'tcp monitoring socket for simple_client',
		:data_flow_id => 10001,
		:connect_address => '127.0.0.1',
		:connect_port => 20001,
		:nodelay => 1
	}
}
# The formatted string of options are built in to an options table and then passed to stk_create_env
envopts = Stk_options.new(opts_hash)
stkbase = Stk_env.new(envopts)

# Create our simple client service using a random ID, monitoring is inherited from the environment
svc_id = rand(0x7fffffffffffffff)

svc = Stk_service.new(stkbase, @opts.service_name, svc_id,
		Stkservice::STK_SERVICE_TYPE_DATA,nil)

# Set this service to a running state so folks know we are in good shape
svc.set_state(Stkservice::STK_SERVICE_STATE_RUNNING)

# Start the application logic updating the checkpoint of the service and
# invoke the timer handler to allow heartbeats to be sent
x = 1
checkpoint = 0
 
while true do
	# Do some application logic
	x = x * 2
	puts "The new value of x is #{x}"

	# Update this service's checkpoint so others know we are doing sane things
	svc.update_smartbeat_checkpoint(checkpoint)
	checkpoint += 1
	stkbase.dispatch_timer_pools(0)

	# See if its time to end
	break if @opts.end_threshold > 0 and checkpoint > @opts.end_threshold
	sleep(1)
end

# Set this service to state 'stopping' so folks know we are in ending
svc.set_state(Stkservice::STK_SERVICE_STATE_STOPPING)

# Ok, now we can get rid of the service
svc.close()

# And get rid of the environment, we are done!
stkbase.close()

# Now free the options that were built
# Because there was nested options, we must free each nest individually
# because there is no stored indication which options are nested
envopts.free_built_options(envopts.find_sub_option("monitoring_data_flow_options"))
envopts.free_built_options(envopts.find_sub_option("name_server_options"))
envopts.free_built_options(nil)
envopts.close()

puts "#{@opts.service_name} hit its threshold of #{@opts.end_threshold}"
