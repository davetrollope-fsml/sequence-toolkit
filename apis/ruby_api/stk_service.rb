require 'stk_env'
require 'stk_service'
require 'stkservice'

class Stk_service
	@_svc = nil;
	def initialize(env,name,id,type,options)
		@_svc = Stkservice.stk_create_service(env.ref(), name, id, type, options ? options.ref() : nil)
	end
	def close(last_state = nil)
		return if !@_svc

		if last_state
			Stkservice.stk_destroy_service_with_state(@_svc,last_state)
		else
			Stkservice.stk_destroy_service(@_svc,nil)
		end
	end
	def set_state(state)
		Stkservice.stk_set_service_state(@_svc,state)
	end
	def update_smartbeat_checkpoint(checkpoint)
		Stkservice.stk_service_update_smartbeat_checkpoint(@_svc,checkpoint)
	end
end

