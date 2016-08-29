require 'stk_env'

class Stk_sequence
	@@obj_map = {}
	def initialize(env,name,id,sequence_type,service_type,options,ref=nil)
		if ref == nil
			@_seq = Stksequence.stk_create_sequence(env.ref(),name,id,sequence_type,service_type,options);
		else
			@_seq = ref;
		end
		@_env = env
		@_id = Stksequence.stk_get_sequence_id(@_seq)
		@_seqptr = Stksequence.stk_seq_ptr_to_ulong_seq(@_seq)
		@@obj_map[@_seqptr] = self
	end
	def close()
		unmap();
		Stksequence.stk_destroy_sequence(@_seq)
	end
	def env()
		return @_env
	end
	def unmap()
		@@obj_map[@_seqptr] = nil
	end
	def hold()
		Stksequence.stk_hold_sequence(@_seq)
	end
	def id()
		return Stksequence.stk_get_sequence_id(@_seq);
	end
	def type()
		return Stksequence.stk_get_sequence_type(@_seq);
	end
	def count()
		return stk_number_of_sequence_elements(@_seq)
	end
	def release()
		stk_destroy_sequence(@_seq)
	end
	def ref()
		return @_seq
	end
	def iterate(clientd)
		n = Stksequence.stk_sequence_first_elem(@_seq)
		while Stksequence.stk_is_at_list_end(n) == 0 do
			dptr = Stksequence.stk_sequence_node_data(n)
			yield(self,dptr,Stksequence.stk_sequence_node_type(n),clientd)
			n = Stksequence.stk_sequence_next_elem(@_seq,n)
		end
	end
	def each(clientd)
		iterate(clientd)
	end
	def copy_array_to(data,user_type)
		v = Stksequence.new_voidDataArray(data.length)
		data.each_with_index do |c,i|
			Stksequence.voidDataArray_setitem(v,i,c)  # Set a value
		end
		Stksequence.stk_copy_chars_to_sequence(@_seq,v,data.length,user_type)
		Stksequence.delete_voidDataArray(v)
	end
	def copy_string_to(data,user_type)
		return Stksequence.stk_copy_string_to_sequence(@_seq,data,user_type)
	end
	def self.find(seqid)
		@@obj_map[seqid]
	end
end
