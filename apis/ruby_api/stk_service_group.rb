require 'stk_service'
require 'stkservice_group'
#require 'stk_sg_automation'
#require 'stk_smartbeat'

class Stk_service_cb < Stkservice_group::Stk_service_cb_class
	def initialize(env,cbcls)
		super()
		@_cbcls = cbcls
		@_env = env
	end
	def close()
		@_cbcls = nil
		@_env = nil
	end
	def added(svcgrpid,svcid,state,svcref)
		rsvg = Stk_service_group.find(svcgrpid)
		rsv = Stk_service.stk_service.find(svcid)
		if rsvg != nil and rsv == nil
			rsv = Stk_service.stk_service(@_env,nil,svcid,0,nil,svcref)
		else
			Stk_env.log(STK_LOG_WARNING,"service group added callback called, but missing object")
			Stk_env.log(STK_LOG_WARNING,"service group: " + rsvg.class.name)
			Stk_env.log(STK_LOG_WARNING,"service: " + rsv.class.name)
		end
		@_cbcls.added_cb(rsvg,rsv,state)
	end
	def removed(svcgrpid,svcid,state)
		rsvg = Stk_service_group.find(svcgrpid)
		rsv = Stk_service.stk_service.find(svcid)
		if rsvg != nil and rsv != nil
			@_cbcls.removed_cb(rsvg,rsv,state)
		else
			Stk_env.log(STK_LOG_WARNING,"service group removed callback called, but missing object")
			Stk_env.log(STK_LOG_WARNING,"service group: " + rsvg.class.name)
			Stk_env.log(STK_LOG_WARNING,"service: " + rsv.class.name)
		end
	end
	def state_chg(svcid,old_state,new_state)
		rsv = Stk_service.stk_service.find(svcid)
		if rsv != nil
			@_cbcls.state_change_cb(rsv,old_state,new_state)
		else
			Stk_env.log(STK_LOG_WARNING,"service state change callback called, but missing object")
			Stk_env.log(STK_LOG_WARNING,"service: " + rsv.class.name)
		end
	end
	def smartbeat(svcgrpid,svcid,smartbeat)
		begin
			rsvg = Stk_service_group.find(svcgrpid)
			rsv = Stk_service.stk_service.find(svcid)
			rsmb = Stk_smartbeat.stk_smartbeat(smartbeat)
			# Need to convert to a smartbeat object
			if rsvg != nil and rsv != nil
				@_cbcls.smartbeat_cb(rsvg,rsv,rsmb)
			else
				Stk_env.log(STK_LOG_WARNING,"service group smartbeat callback called, but missing object")
				Stk_env.log(STK_LOG_WARNING,"service group: " + rsvg.class.name)
				Stk_env.log(STK_LOG_WARNING,"service: " + rsv.class.name)
			end
		rescue Exception => e
			Stk_env.log(STK_LOG_WARNING,str(e))
		end
	end
end

class Stk_service_group
	@@obj_map = {}
	def initialize(env,group_name,id,options)
		@_svcgrp = Stkservice_group::stk_create_service_group(env.ref(),group_name,id,options.ref());
		@@obj_map[Stkservice_group::stk_get_service_group_id(@_svcgrp)] = self
		@_env = env;
	end
	def close(options = nil)
		Stkservice_group::stk_destroy_service_group(@_svcgrp)
	end
	def ref()
		return @_svcgrp
	end
	def name()
		return Stkservice_group::stk_get_service_group_name(@_svcgrp)
	end
	def invoke(seq)
		stk_sg_automation.stk_sga_invoke(@_svcgrp,seq.ref())
	end
	def self.find(svcgrpid)
		if @@obj_map.include? svcgrpid
			return cls.obj_map[svcgrpid]
		end
	end
	def self.add_service_cb_option(env,svcgrp_opts,appcb)
		caller = Stkservice_group::Stk_service_cb_caller.new
		appcb.add_callback_ref(caller)
		svccb = Stk_service_cb.new(env,appcb)
		appcb.add_callback_map_obj(svccb)
		newopts = caller.stk_append_service_cb(svcgrp_opts.ref(),svccb)
		svcgrp_opts.update_ref(newopts)
		return svccb
	end
	def self.remove_service_cb_option(env,svcgrp_opts,appcb)
		caller = appcb.caller()
		svccb = appcb.map_obj()
		caller.stk_remove_service_cb(svcgrp_opts.ref())
		svccb.close() if svccb
	end
end

