class stk_service:
	obj_map = {}
	def __init__(self,env,name,id,type,options,ref=None):
		self._svc = None
		try:
			if options:
				optsref = options.ref()
			else:
				optsref = None
			if ref == None:
				self._svc = stk_create_service(env.ref(),name,id,type,optsref)
				if self._svc == None:
					raise Exception('service failed')
			else:
				self._svc = ref
			self.__class__.obj_map[stk_get_service_id(self._svc)] = self
		except Exception, e:
			env.__class__.log(STK_LOG_ERROR,"Failed to create service " + str(id) + ": " + str(e))
			raise
	def set_state(self,state):
		stk_set_service_state(self._svc,state)
	def update_smartbeat_checkpoint(self,checkpoint):
		stk_service_update_smartbeat_checkpoint(self._svc,checkpoint)
	def close(self,last_state = None):
		if self._svc:
			self.__class__.obj_map[stk_get_service_id(self._svc)] = None
			if last_state:
				stk_destroy_service_with_state(self._svc,last_state)
			else:
				stk_destroy_service(self._svc,None)
			self._svc = None
			#del self.__class__.obj_map[stk_get_service_id(self._svc)]
	def ref(self):
		return self._svc
	def name(self):
		return stk_get_service_name(self._svc)
	def state_str(self,state):
		new_str = stk_get_service_state_str_sz(self._svc,state,STK_SERVICE_STATE_NAME_MAX);
		return new_str
	@classmethod
	def find(cls,svcid):
		if svcid in cls.obj_map:
			return cls.obj_map[svcid]
