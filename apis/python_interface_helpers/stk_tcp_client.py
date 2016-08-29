from stk_data_flow import *
class stk_tcp_client:
	def __init__(self,env,name,id,options,ref=None):
		if ref == None:
			self._df = stk_tcp_client_create_data_flow(env.ref(),name,id,options.ref())
			if self._df == None:
				raise Exception("Failed to create tcp client data flow")
		else:
			self._df = ref
	def close(self):
		stk_destroy_data_flow(self._df)
	def ref(self):
		return self._df
	def id(self):
		return stk_get_data_flow_id(self._df)
	def send(self,seq,flags):
		return stk_data_flow_send(self._df,seq.ref(),flags)

class stk_tcp_subscriber(stk_tcp_client):
	pass
