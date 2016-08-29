from stk_sequence import *
from stk_tcp_server import *
from stk_tcp_client import *
from stk_data_flow import *
from stk_options import stk_clear_cb
import time

class stk_callback:
	def __init__(self):
		self._caller = None
		self._mapobj = None
		pass
	def add_callback_ref(self,caller):
		self._caller = caller
	def del_callback_ref(self,caller):
		if self._caller:
			self._caller.delCallback()
	def add_callback_map_obj(self,mapobj):
		self._mapobj = mapobj
	def map_obj(self):
		return self._mapobj
	def close(self):
		if self._caller:
			self.del_callback_ref(self._caller)
			self._caller = None
	def caller(self):
		return self._caller
	def fd_created(self,df,fd):
		pass
	def fd_destroyed(self,df,fd):
		pass

class stk_dispatcher_cb(stk_dispatch_cb_class):
	def __init__(self,env,cbcls):
		self.dispatchclass = stk_dispatch_cb_class.__init__(self)
		self._cbcls = cbcls
		self._env = env
	def close(self):
		#del self.dispatchclass
		#self.dispatchclass = None
		#del self.__class__.obj_map[stk_get_service_group_id(self._svcgrp)]
		pass
	def finddf(self,dfptr):
		dfref = stk_ulong_df_to_df_ptr(dfptr)
		df = stk_data_flow.find(dfptr)
		if df == None:
			dftype = stk_data_flow.type(dfptr)
			if dftype == STK_TCP_ACCEPTED_FLOW or dftype == STK_TCP_SERVER_FLOW:
				df = stk_tcp_server(self._env,None,None,None,dfref)
			if dftype == STK_TCP_CLIENT_FLOW:
				df = stk_tcp_client(self._env,None,None,None,dfref)
		return df
	def process_data(self,dfptr,seqptr):
		seqref = stk_ulong_seq_to_seq_ptr(seqptr)
		seq = stk_sequence.find(seqptr)
		if seq == None:
			seq = stk_sequence(self._env,None,None,0,0,None,seqref)
		# the dfptr here is actually the C pointer converted to a ulong
		df = self.finddf(dfptr)
		self._cbcls.process_data(df,seq)
		seq.unmap()
	def process_name_response(self,dfptr,seqptr):
		seqref = stk_ulong_seq_to_seq_ptr(seqptr)
		seq = stk_sequence.find(seqptr)
		if seq == None:
			seq = stk_sequence(self._env,None,None,0,0,None,seqref)
		# the dfptr here is actually the C pointer converted to a ulong
		df = self.finddf(dfptr)
		self._cbcls.process_name_response(df,seq)
		seq.unmap()
		pass
	def process_monitoring_response(self,dfptr,seqptr):
		pass
	def fd_created(self,dfptr,fd):
		# the dfptr here is actually the C pointer converted to a ulong
		dfref = stk_ulong_df_to_df_ptr(dfptr)
		df = stk_data_flow.find(dfptr)
		if df == None:
			# This sucks....
			dftype = stk_data_flow.type(dfptr)
			if dftype == STK_TCP_ACCEPTED_FLOW or dftype == STK_TCP_SERVER_FLOW:
				df = stk_tcp_server(self._env,None,None,None,dfref)
			# Err, UDP doesn't actually have connections so this really
			# isn't likely to be needed - why would the app care about udp creations?
			#elif dftype == STK_UDP_CLIENT_FLOW:
				#df = stk_udp_client(self._env,None,None,None,dfref)
		if df:
			self._cbcls.fd_created(df,fd)
	def fd_destroyed(self,dfptr,fd):
		# the dfptr here is actually the C pointer converted to a ulong
		dfref = stk_ulong_df_to_df_ptr(dfptr)
		df = stk_data_flow.find(dfptr)
		if df == None:
			dftype = stk_data_flow.type(dfptr)
			if dftype == STK_TCP_ACCEPTED_FLOW or dftype == STK_TCP_SERVER_FLOW:
				df = stk_tcp_server(self._env,None,None,None,dfref)
		if df:
			self._cbcls.fd_destroyed(df,fd)

class stk_env:
	def __init__(self,envopts):
		self.caller = stk_dispatch_cb_caller()
		envopts.append_dispatcher(self.caller.get_dispatcher())
		self._opts = envopts
		self._env = stk_create_env(envopts.ref())
		self._dispatcher_stopped = False;
	def close(self):
		if self._env:
			if self._opts:
				stk_clear_cb(self._opts.ref(),"dispatcher")
			if self.caller:
				self.caller.detach_env(self._env)
			stk_destroy_env(self._env)
			if self.caller:
				self.caller.close()
				self.caller = None
			self._env = None
	def ref(self):
		return self._env
	def get_name_service(self):
		return stk_env_get_name_service(self.ref())
	def dispatch_timer_pools(self,interval):
		stk_env_dispatch_timer_pools(self._env,interval)
	def listening_dispatcher(self,df,svcgrp,appcb):
		appcb.add_callback_ref(self.caller)
		self._dispatcher_stopped = False
		if self.caller.env_listening_dispatcher_add_fd(df.ref()) < 0:
			return
		while self._dispatcher_stopped == False:
			self.caller.env_listening_dispatcher(df.ref(),stk_dispatcher_cb(self,appcb).__disown__(),200)
		self.caller.env_listening_dispatcher_del_fd(df.ref())
	def client_dispatcher_timed(self,appcb,timeout):
		if appcb:
			appcb.add_callback_ref(self.caller)
			self.caller.env_client_dispatcher_timed(self._env,timeout,stk_dispatcher_cb(self,appcb).__disown__())
		else:
			self.caller.env_client_dispatcher_timed(self._env,timeout,None)
	def stop_dispatcher(self):
		self._dispatcher_stopped = True;
		self.caller.env_stop_dispatching(self._env)
		time.sleep(.2)
	def terminate_dispatcher(self):
		self.caller.env_terminate_dispatcher(self._env)
	@classmethod
	def append_name_server_dispatcher_cbs(cls,envopts,data_flow_group):
		nsopts = envopts.find_option("name_server_options")
		nsopts.update_ref(stk_append_name_server_fd_cbs(data_flow_group,nsopts.ref()))
	@classmethod
	def remove_name_server_dispatcher_cbs(cls,envopts,data_flow_group):
		dfopts = envopts.find_option(data_flow_group + "_options")
		if dfopts != None:
			dfopts.remove_dispatcher_fd_cbs()
		else:
			envopts.remove_dispatcher_fd_cbs()
	@classmethod
	def append_monitoring_dispatcher_cbs(cls,envopts,data_flow_group):
		envopts.update_ref(stk_append_monitoring_fd_cbs(data_flow_group,envopts.ref()))
	@classmethod
	def remove_monitoring_dispatcher_cbs(cls,envopts,data_flow_group):
		dfopts = envopts.find_option(data_flow_group + "_options")
		if dfopts != None:
			dfopts.remove_dispatcher_fd_cbs()
	@classmethod
	def log(cls,level,message):
		stk_log(level,message)
	@classmethod
	def debug(cls,component,message):
		stk_debug(component,message)

