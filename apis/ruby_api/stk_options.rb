require 'stkoptions'

class Stk_options
	@_opts = nil
	def ref()
		@_opts
	end

	def initialize(opts_str,opts=nil)
		if !opts.nil?
			@_opts = opts
		else
			opts_str = Stk_options.hash_to_string(opts_str) if opts_str.is_a?(Hash)
			@_opts = Stkoptions.stk_build_options(opts_str,opts_str.length)
		end
	end

	def find_option(name)
		opt = Stkoptions.stk_find_option(@_opts,name,nil);
		if !opt.nil?
			return Stk_options.new(nil,Stkoptions.stk_void_to_options_t(opt))
		else
			return nil
		end
	end

	def find_sub_option(name)
		Stkoptions.find_sub_option(@_opts,name)
	end

	def free_built_options(opts)
		if opts
			Stkoptions.stk_free_built_options(opts)
		else
			Stkoptions.stk_free_built_options(@_opts)
		end
	end

	def close()
		Stkoptions.stk_free_options(@_opts) if @_opts
	end

	def append_sequence(option_name,sequence)
		@_opts = Stkoptions.stk_append_sequence(@_opts,option_name,sequence.ref())
	end

	def update_ref(newref)
		@_opts = newref
	end

	def append_dispatcher(dispatcher)
		@_opts = Stkoptions.stk_append_dispatcher(@_opts,dispatcher)
	end

	def append_data_flow(data_flow_group,df)
		@_opts = Stkoptions.stk_append_data_flow(@_opts,data_flow_group,df.ref())
	end

	def remove_data_flow(name)
		Stkoptions.stk_clear_cb(@_opts,name) if @_opts
	end

	def append_dispatcher_fd_cbs(data_flow_group)
		@_opts = Stkoptions.stk_option_append_dispatcher_fd_cbs(data_flow_group,@_opts)
	end

	def remove_dispatcher_fd_cbs()
		if @_opts
			Stkoptions.stk_clear_cb(@_opts,"fd_created_cb")
			Stkoptions.stk_clear_cb(@_opts,"fd_destroyed_cb")
		end
	end

	def self.hash_to_string(h,indent=0)
		s = ""
		h.each do |k,v|
			if v.is_a?(Hash)
				indent.times { s = s + " " }
				s = s + "#{k.to_s}\n"
				s = s + self.hash_to_string(v,indent+1)
			else
				indent.times { s = s + " " }
				s = s + "#{k.to_s} #{v}\n"
			end
		end
		s
	end
end

