require 'stkdata_flow'
class Stk_data_flow
	@@obj_map = {}
	def self.type(df)
		# Not sure why I can't use stk_get_data_flow_type() here
		Stkdata_flow.stk_dftype_ulong(df)
	end
	def self.find(dfid)
		return @@obj_map[dfid]
	end
	def self.map(dfid,obj)
		@@obj_map[dfid] = obj
	end
	def self.unmap(dfid)
		@@obj_map[dfid] = nil
	end
end
