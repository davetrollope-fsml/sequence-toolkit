class stk_options:
	def __init__(self,opts_str,_opts=None):
		if _opts != None:
			self._opts = _opts
		else:
			if type(opts_str) is dict:
				opts_str = self.hash_to_string(opts_str)
			self._opts = stk_build_options(opts_str,len(opts_str))
	def ref(self):
		return self._opts
	def find_option(self,name):
		opt = stk_find_option(self._opts,name,None);
		if opt != None:
			return stk_options(None,stk_void_to_options_t(opt))
		else:
			return None
	def find_sub_option(self,name):
		subopts = stk_find_option(self._opts,name,None);
		return stk_void_to_options_t(subopts);
	def free_built_options(self,opts):
		if opts != None:
			stk_free_built_options(opts)
		else:
			stk_free_built_options(self._opts)
	def free_sub_option(self,optname):
		self.free_built_options(self.find_sub_option(optname))
	def close(self):
		if self._opts != None:
			self.free_built_options(None)
			stk_free_options(self._opts)
			self._opts = None
	def append_dispatcher_wakeup_cb(self):
		self._opts = stk_append_dispatcher_wakeup_cb(self._opts)
	def remove_dispatcher_wakeup_cb(self):
		if self._opts != None:
			stk_clear_cb(self._opts,"wakeup_cb")
	def append_dispatcher_fd_cbs(self,data_flow_group):
		self._opts = stk_option_append_dispatcher_fd_cbs(data_flow_group,self._opts)
	def remove_dispatcher_fd_cbs(self):
		if self._opts != None:
			stk_clear_cb(self._opts,"fd_created_cb")
			stk_clear_cb(self._opts,"fd_destroyed_cb")
	def remove_data_flow(self,name):
		if self._opts != None:
			stk_clear_cb(self._opts,name)
	def append_dispatcher(self,dispatcher):
		self._opts = stk_append_dispatcher(self._opts,dispatcher)
	def append_data_flow(self,data_flow_group,df):
		self._opts = stk_append_data_flow(self._opts,data_flow_group,df.ref())
	def remove_data_flow(self,data_flow_group):
		if self._opts != None:
			stk_clear_cb(self._opts,data_flow_group)
	def append_sequence(self,option_name,sequence):
		self._opts = stk_append_sequence(self._opts,option_name,sequence.ref())
	def remove_sequence(self,option_name):
		if self._opts != None:
			stk_clear_cb(self._opts,option_name)
	def stk_add_callback_option(self,name,cb):
		return None
	def update_ref(self,newref):
		self._opts = newref
	@classmethod
	def hash_to_string(cls,d,indent=0):
		s = ""
		for k, v in d.iteritems():
			if type(v) is dict:
				for _ in range(indent):
					s = s + " "
				s = s + str(k) + "\n"
				s = s + stk_options.hash_to_string(v,indent+1)
			else:
				for _ in range(indent):
					s = s + " "
				s = s + str(k) + " " + str(v) + "\n"
		return s
