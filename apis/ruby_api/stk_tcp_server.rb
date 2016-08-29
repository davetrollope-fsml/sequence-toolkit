require 'stk_data_flow'
require 'stkdata_flow'
require 'stktcp_server'
class Stk_tcp_server
	def initialize(env,name,id,options,ref=nil)
		@_env = env
		if ref.nil?
			@_df = Stktcp_server.stk_tcp_server_create_data_flow(env.ref(),name,id,options.ref())
			raise "Failed to create tcp server data flow" if @_df.nil?
		else
			@_df = ref
		end
		@_id = Stkdata_flow.stk_get_data_flow_id(@_df)
		@_dfptr = Stkdata_flow.stk_df_ptr_to_ulong_df(@_df)
		Stk_data_flow.map(@_dfptr,self);
	end
	def close()
		if !@_id.nil?
			Stk_data_flow.unmap(@_dfptr)
			Stkdata_flow.stk_destroy_data_flow(@_df)
		end
	end
	def ref()
		@_df
	end
	def id()
		Stkdata_flow.stk_get_data_flow_id(@_df)
	end
	def send(seq,flags)
		Stkdata_flow.stk_data_flow_send(@_df,seq.ref(),flags)
	end
	def env()
		@_env
	end
end

class Stk_tcp_publisher < Stk_tcp_server
end
