from stk_data_flow import *
class stk_data_flow:
	obj_map = {}
	@classmethod
	def type(cls,df):
		# Not sure why I can't use stk_get_data_flow_type() here
		return stk_dftype_ulong(df)
	@classmethod
	def find(cls,dfid):
		if dfid in cls.obj_map:
			return cls.obj_map[dfid]
	@classmethod
	def map(cls,dfid,obj):
		cls.obj_map[dfid] = obj
	@classmethod
	def unmap(cls,dfid):
		cls.obj_map[dfid] = None
