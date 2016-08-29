import stk_service
import stk_sg_automation
import stk_smartbeat

class stk_service_cb(stk_service_cb_class):
	def __init__(self,env,cbcls):
		stk_service_cb_class.__init__(self)
		self._cbcls = cbcls
		self._env = env
	def close(self):
		self._cbcls = None
		self._env = None
	def added(self,svcgrpid,svcid,state,svcref):
		rsvg = stk_service_group.find(svcgrpid)
		rsv = stk_service.stk_service.find(svcid)
		if rsvg != None and rsv == None:
			rsv = stk_service.stk_service(self._env,None,svcid,0,None,svcref)
		else:
			self._env.__class__.log(STK_LOG_WARNING,"service group added callback called, but missing object")
			self._env.__class__.log(STK_LOG_WARNING,"service group: " + rsvg.__class__.__name__)
			self._env.__class__.log(STK_LOG_WARNING,"service: " + rsv.__class__.__name__)
		self._cbcls.added_cb(rsvg,rsv,state)
	def removed(self,svcgrpid,svcid,state):
		rsvg = stk_service_group.find(svcgrpid)
		rsv = stk_service.stk_service.find(svcid)
		if rsvg != None and rsv != None:
			self._cbcls.removed_cb(rsvg,rsv,state)
		else:
			self._env.__class__.log(STK_LOG_WARNING,"service group removed callback called, but missing object")
			self._env.__class__.log(STK_LOG_WARNING,"service group: " + rsvg.__class__.__name__)
			self._env.__class__.log(STK_LOG_WARNING,"service: " + rsv.__class__.__name__)
	def state_chg(self,svcid,old_state,new_state):
		rsv = stk_service.stk_service.find(svcid)
		if rsv != None:
			self._cbcls.state_change_cb(rsv,old_state,new_state)
		else:
			self._env.__class__.log(STK_LOG_WARNING,"service state change callback called, but missing object")
			self._env.__class__.log(STK_LOG_WARNING,"service: " + rsv.__class__.__name__)
	def smartbeat(self,svcgrpid,svcid,smartbeat):
		try:
			rsvg = stk_service_group.find(svcgrpid)
			rsv = stk_service.stk_service.find(svcid)
			rsmb = stk_smartbeat.stk_smartbeat(smartbeat)
			# Need to convert to a smartbeat object
			if rsvg != None and rsv != None:
				self._cbcls.smartbeat_cb(rsvg,rsv,rsmb)
			else:
				self._env.__class__.log(STK_LOG_WARNING,"service group smartbeat callback called, but missing object")
				self._env.__class__.log(STK_LOG_WARNING,"service group: " + rsvg.__class__.__name__)
				self._env.__class__.log(STK_LOG_WARNING,"service: " + rsv.__class__.__name__)
		except Exception, e:
			self._env.__class__.log(STK_LOG_WARNING,str(e))

class stk_service_group:
	obj_map = {}
	def __init__(self,env,group_name,id,options):
		self._svcgrp = stk_create_service_group(env.ref(),group_name,id,options.ref());
		self.__class__.obj_map[stk_get_service_group_id(self._svcgrp)] = self
		self._env = env;
	def close(self,options = None):
		stk_destroy_service_group(self._svcgrp)
		#del self.__class__.obj_map[stk_get_service_group_id(self._svcgrp)]
	def ref(self):
		return self._svcgrp
	def name(self):
		return stk_get_service_group_name(self._svcgrp)
	def invoke(self,seq):
		stk_sg_automation.stk_sga_invoke(self._svcgrp,seq.ref())
	@classmethod
	def find(cls,svcgrpid):
		if svcgrpid in cls.obj_map:
			return cls.obj_map[svcgrpid]
	@classmethod
	def add_service_cb_option(cls,env,svcgrp_opts,appcb):
		caller = stk_service_cb_caller()
		appcb.add_callback_ref(caller)
		svccb = stk_service_cb(env,appcb)
		appcb.add_callback_map_obj(svccb)
		newopts = caller.stk_append_service_cb(svcgrp_opts.ref(),svccb.__disown__())
		svcgrp_opts.update_ref(newopts)
		return svccb
	@classmethod
	def remove_service_cb_option(cls,env,svcgrp_opts,appcb):
		caller = appcb.caller()
		svccb = appcb.map_obj()
		caller.stk_remove_service_cb(svcgrp_opts.ref())
		if svccb:
			svccb.close()

