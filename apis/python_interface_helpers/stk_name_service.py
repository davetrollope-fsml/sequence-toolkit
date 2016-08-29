from stk_env import stk_env_get_name_service
import stk_sequence
class stk_name_info:
	def __init__(self,name_info_ref):
		self._name_info = name_info_ref
	def name(self):
		return stk_name_from_name_info(self._name_info)
	def ip(self,idx):
		return stk_get_ip_from_name_info(self._name_info,idx)
	def meta_data(self,env):
		seq_ref = stk_sequence_from_name_info(self._name_info)
		return stk_sequence.stk_sequence(env,None,None,None,None,None,seq_ref)
	def ft_state(self):
		return self._name_info.ft_state

class stk_name_service_cb(stk_name_service_cb_class):
	def __init__(self,env,cbcls):
		stk_name_service_cb_class.__init__(self)
		self._cbcls = cbcls
		self._env = env
	def close(self):
		self._cbcls = None
		self._env = None
	def name_service_info_cb(self,name_info,name_count,server_info,app_info,cb_type):
		self._cbcls.name_info_cb(stk_name_info(name_info),server_info,app_info,cb_type) 

class stk_name_service:
	@classmethod
	def register_name(cls,stkbase,name,linger,expiration_ms,name_info_cb,app_info,options):
		if options:
			options_ref = options.ref()
		else:
			options_ref = None

		if name_info_cb == None:
			return stk_register_name_nocb(stk_env_get_name_service(stkbase.ref()), name, linger, expiration_ms, app_info, options_ref)

		caller = stk_name_service_cb_caller()
		name_info_cb.add_callback_ref(caller)
		namesvccb = stk_name_service_cb(stkbase,name_info_cb)
		name_info_cb.add_callback_map_obj(namesvccb)
		name_info_cb.app_info = app_info

		return caller.stk_register_name_cls(stk_env_get_name_service(stkbase.ref()),name,linger,expiration_ms,namesvccb,None,options_ref)

	@classmethod
	def invoke(cls,seq):
		r = seq.ref()
		return stk_name_service_invoke(seq.ref())

	@classmethod
	def request_name_info(cls,stkbase,name,expiration_ms,name_info_cb,app_info,options):
		if options:
			options_ref = options.ref()
		else:
			options_ref = None

		caller = stk_name_service_cb_caller()
		name_info_cb.add_callback_ref(caller)
		namesvccb = stk_name_service_cb(stkbase,name_info_cb)
		name_info_cb.add_callback_map_obj(namesvccb)
		name_info_cb.app_info = app_info

		return caller.stk_request_name_info_cls(stkbase.get_name_service(),name,expiration_ms,namesvccb,None,options_ref)

	@classmethod
	def subscribe_to_name_info(cls,stkbase,name,name_info_cb,app_info,options):
		if options:
			options_ref = options.ref()
		else:
			options_ref = None

		caller = stk_name_service_cb_caller()
		name_info_cb.add_callback_ref(caller)
		namesvccb = stk_name_service_cb(stkbase,name_info_cb)
		name_info_cb.add_callback_map_obj(namesvccb)
		name_info_cb.app_info = app_info

		return caller.stk_subscribe_name_info_cls(stk_env_get_name_service(stkbase.ref()),name,namesvccb,app_info,options_ref)

