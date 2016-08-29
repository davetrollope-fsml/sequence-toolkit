from stk_data_flow import *
class stk_tcp_server:
	def __init__(self,env,name,id,options,ref=None):
		if ref == None:
			self._df = stk_tcp_server_create_data_flow(env.ref(),name,id,options.ref())
			if self._df == None:
				raise Exception("Failed to create tcp server data flow")
		else:
			self._df = ref
		self._id = stk_get_data_flow_id(self._df)
		self._dfptr = stk_df_ptr_to_ulong_df(self._df)
		stk_data_flow.map(self._dfptr,self);
	def close(self):
		if self._id != None:
			stk_data_flow.unmap(self._dfptr)
			stk_destroy_data_flow(self._df)
	def ref(self):
		return self._df
	def id(self):
		return stk_get_data_flow_id(self._df)
	def send(self,seq,flags):
		return stk_data_flow_send(self._df,seq.ref(),flags)

class stk_tcp_publisher(stk_tcp_server):
	pass
