require 'stk_data_flow'
require 'stkdata_flow'
require 'stktcp_client'
class Stk_tcp_client
	def initialize(env,name,id,options,ref=nil)
		@_env = env
		if ref.nil?
			@_df = Stktcp_client.stk_tcp_client_create_data_flow(env.ref(),name,id,options.ref())
			raise "Failed to create tcp client data flow" if @_df.nil?
		else
			@_df = ref
		end
	end
	def close()
		Stkdata_flow.stk_destroy_data_flow(@_df)
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

class Stk_tcp_subscriber < Stk_tcp_client
end

