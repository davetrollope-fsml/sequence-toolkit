require 'stkenv'
require 'stkname_service'

class Stk_name_info
	def initialize(name_info_ref)
		@_name_info = name_info_ref
	end
	def name()
		Stkname_service.stk_name_from_name_info(@_name_info)
	end
	def ip(idx)
		Stkname_service.stk_get_ip_from_name_info(@_name_info,idx)
	end
	def meta_data(env)
		seq_ref = Stkname_service.stk_sequence_from_name_info(@_name_info)
		Stk_sequence.new(env,nil,nil,nil,nil,nil,seq_ref)
	end
	def ft_state()
		@_name_info.ft_state
	end
end

class Stk_name_service_cb < Stkname_service::Stk_name_service_cb_class
	def initialize(env,cbcls)
		super()
		@_cbcls = cbcls
		@_env = env
	end
	def close
		self._cbcls = nil
		self._env = nil
	end
	def name_service_info_cb(name_info,name_count,server_info,app_info,cb_type)
		@_cbcls.name_info_cb(Stk_name_info.new(name_info),server_info,app_info,cb_type) 
	end
end

class Stk_name_service
	def self.register_name(stkbase,name,linger,expiration_ms,name_info_cb,app_info,options)
		if options
			options_ref = options.ref()
		else
			options_ref = nil
		end

		return stk_register_name_nocb(stk_env_get_name_service(stkbase.ref()), name, linger, expiration_ms, app_info, options_ref) if name_info_cb.nil?

		cb_caller = Stkname_service::Stk_name_service_cb_caller.new
		name_info_cb.add_callback_ref(cb_caller)
		namesvccb = Stk_name_service_cb.new(stkbase,name_info_cb)
		name_info_cb.add_callback_map_obj(namesvccb)
		name_info_cb.app_info = app_info

		return cb_caller.stk_register_name_cls(stkbase.get_name_service(),name,linger,expiration_ms,namesvccb,nil,options_ref)
	end
	def self.invoke(seq)
		r = seq.ref()
		Stkname_service.stk_name_service_invoke(seq.ref())
	end
	def self.request_name_info(stkbase,name,expiration_ms,name_info_cb,app_info,options)
		if options
			options_ref = options.ref()
		else
			options_ref = nil
		end

		cb_caller = Stkname_service::Stk_name_service_cb_caller.new
		name_info_cb.add_callback_ref(cb_caller)
		namesvccb = Stk_name_service_cb.new(stkbase,name_info_cb)
		name_info_cb.add_callback_map_obj(namesvccb)
		name_info_cb.app_info = app_info

		return cb_caller.stk_request_name_info_cls(stkbase.get_name_service(),name,expiration_ms,namesvccb,nil,options_ref)
	end
	def self.subscribe_to_name_info(stkbase,name,name_info_cb,app_info,options)
		if options
			options_ref = options.ref()
		else
			options_ref = nil
		end

		cb_caller = Stkname_service::Stk_name_service_cb_caller.new
		name_info_cb.add_callback_ref(cb_caller)
		namesvccb = Stk_name_service_cb.new(stkbase,name_info_cb)
		name_info_cb.add_callback_map_obj(namesvccb)
		name_info_cb.app_info = app_info

		return cb_caller.stk_subscribe_name_info_cls(stkbase.get_name_service(),name,namesvccb,nil,options_ref)
	end
end

