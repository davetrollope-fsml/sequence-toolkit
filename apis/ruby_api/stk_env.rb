require 'stk_env'
require 'stkenv'
require 'stksequence'
require 'stk_sequence'
require 'stkdata_flow'
require 'stk_data_flow'
require 'stk_tcp_server'
require 'stk_tcp_client'

class Stk_callback
	def initialize
		@_caller = nil
		@_mapobj = nil
	end
	def add_callback_ref(caller)
		if @_caller
			@_caller.delCallback()
		end
		@_caller = caller
	end
	def del_callback_ref(caller)
		if @_caller
			@_caller.delCallback()
		end
	end
	def add_callback_map_obj(mapobj)
		@_mapobj = mapobj
	end
	def map_obj
		return @_mapobj
	end
	def close
		if @_caller
			del_callback_ref(@_caller)
			@_caller = nil
		end
	end
	def caller
		@_caller
	end
	def fd_created(df,fd)
	end
	def fd_destroyed(df,fd)
	end
end

class Stk_dispatcher_cb < Stkenv::Stk_dispatch_cb_class
	def initialize(env,cbcls)
		super()
		@_cbcls = cbcls
		@_env = env
	end
	def close()
	end
	def finddf(dfptr)
		dfref = Stkdata_flow.stk_ulong_df_to_df_ptr(dfptr)
		df = Stk_data_flow.find(dfptr)
		if df.nil?
			dftype = Stk_data_flow.type(dfptr)
			case dftype
			when Stkenv::STK_TCP_ACCEPTED_FLOW, Stkenv::STK_TCP_SERVER_FLOW
				df = Stk_tcp_server.new(@_env,nil,nil,nil,dfref)
			when Stkenv::STK_TCP_CLIENT_FLOW
				df = Stk_tcp_client.new(@_env,nil,nil,nil,dfref)
			end
		end
		df
	end
	def process_data(dfptr,seqptr)
		seqref = Stksequence.stk_ulong_seq_to_seq_ptr(seqptr)
		seq = Stk_sequence.find(seqptr)
		seq = Stk_sequence.new(@_env,nil,nil,0,0,nil,seqref) if seq.nil?
		# the dfptr here is actually the C pointer converted to a ulong
		df = finddf(dfptr)
		@_cbcls.process_data(df,seq)
		seq.unmap()
	end
	def process_name_response(dfptr,seqptr)
		seqref = Stksequence.stk_ulong_seq_to_seq_ptr(seqptr)
		seq = Stk_sequence.find(seqptr)
		seq = Stk_sequence.new(@_env,nil,nil,0,0,nil,seqref) if seq.nil?
		# the dfptr here is actually the C pointer converted to a ulong
		df = finddf(dfptr)
		@_cbcls.process_name_response(df,seq)
		seq.unmap()
	end
	def process_monitoring_response(dfptr,seqptr)
	end
	def fd_created(dfptr,fd)
		# the dfptr here is actually the C pointer converted to a ulong
		dfref = Stkdata_flow.stk_ulong_df_to_df_ptr(dfptr)
		df = Stk_data_flow.find(dfptr)
		if df.nil?
			# This sucks....
			dftype = Stk_data_flow.type(dfptr)
			if dftype == Stkenv::STK_TCP_ACCEPTED_FLOW or dftype == Stkenv::STK_TCP_SERVER_FLOW
				df = Stk_tcp_server.new(@_env,nil,nil,nil,dfref)
			end
			# Err, UDP doesn't actually have connections so this really
			# isn't likely to be needed - why would the app care about udp creations?
			#elif dftype == STK_UDP_CLIENT_FLOW:
				#df = stk_udp_client(@_env,nil,nil,nil,dfref)
		end
		@_cbcls.fd_created(df,fd) if df
	end
	def fd_destroyed(dfptr,fd)
		# the dfptr here is actually the C pointer converted to a ulong
		dfref = Stkdata_flow.stk_ulong_df_to_df_ptr(dfptr)
		df = Stk_data_flow.find(dfptr)
		if df.nil?
			dftype = Stk_data_flow.type(dfptr)
			if dftype == Stkenv::STK_TCP_ACCEPTED_FLOW or dftype == Stkenv::STK_TCP_SERVER_FLOW
				df = Stk_tcp_server.new(@_env,nil,nil,nil,dfref)
			end
		end
		@_cbcls.fd_destroyed(df,fd) if df
	end
end

class Stk_env
	@_env = nil
	def ref()
		@_env
	end

	@_caller = nil
	def initialize(envopts)
		@_caller = Stkenv::Stk_dispatch_cb_caller.new
		envopts.append_dispatcher(@_caller.get_dispatcher())
		@_opts = envopts
		@_env = Stkenv.stk_create_env(envopts.ref())
		@_dispatcher_stopped = false;
	end

	def close()
		Stkenv.stk_destroy_env(@_env) if @_env
	end

	def dispatch_timer_pools(interval)
		Stkenv.stk_env_dispatch_timer_pools(@_env,interval)
	end

	def get_name_service
		Stkenv.stk_env_get_name_service(ref())
	end

	def listening_dispatcher(df,svcgrp,appcb)
		appcb.add_callback_ref(@_caller)
		@_dispatcher_stopped = false
		return if @_caller.env_listening_dispatcher_add_fd(df.ref()) < 0
		while !@_dispatcher_stopped do
			@_caller.env_listening_dispatcher(df.ref(),Stk_dispatcher_cb.new(self,appcb),200)
		end
		@_caller.env_listening_dispatcher_del_fd(df.ref())
	end

	def client_dispatcher_timed(appcb,timeout)
		if appcb
			appcb.add_callback_ref(@_caller)
			@_caller.env_client_dispatcher_timed(ref(),timeout,Stk_dispatcher_cb.new(self,appcb))
		else
			@_caller.env_client_dispatcher_timed(ref(),timeout,nil)
		end
	end
	def stop_dispatcher()
		@_dispatcher_stopped = true;
		@_caller.env_stop_dispatching(ref())
		sleep(0.2)
	end
	def self.append_name_server_dispatcher_cbs(envopts,data_flow_group)
		nsopts = envopts.find_option("name_server_options")
		nsopts.update_ref(Stkenv.stk_append_name_server_fd_cbs(data_flow_group,nsopts.ref()))
	end
	def self.append_monitoring_dispatcher_cbs(envopts,data_flow_group)
		envopts.update_ref(Stkenv.stk_append_name_server_fd_cbs(data_flow_group,envopts.ref()))
	end
	def self.log(level,message)
		stk_log(level,message)
	end
end

