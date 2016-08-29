import stk_env
class stk_sequence:
	obj_map = {}
	def __init__(self,env,name,id,sequence_type,service_type,options,ref=None):
		if ref == None:
			self._seq = stk_create_sequence(env.ref(),name,id,sequence_type,service_type,options);
		else:
			self._seq = ref;
		self._env = env
		self._id = stk_get_sequence_id(self._seq)
		self._seqptr = stk_seq_ptr_to_ulong_seq(self._seq)
		self.__class__.obj_map[self._seqptr] = self
	def close(self):
		self.unmap();
		stk_destroy_sequence(self._seq)
	def env(self):
		return self._env
	def unmap(self):
		self.__class__.obj_map[self._seqptr] = None
	def hold(self):
		stk_hold_sequence(self._seq)
	def id(self):
		return stk_get_sequence_id(self._seq);
	def type(self):
		return stk_get_sequence_type(self._seq);
	def count(self):
		return stk_number_of_sequence_elements(self._seq)
	def release(self):
		stk_destroy_sequence(self._seq)
	def ref(self):
		return self._seq
	def iterate(self,cb,clientd):
		n = stk_sequence_first_elem(self._seq)
		while stk_is_at_list_end(n) == 0:
			dptr = stk_sequence_node_data(n)
			cb(self,dptr,stk_sequence_node_type(n),clientd)
			n = stk_sequence_next_elem(self._seq,n)
	def copy_array_to(self,data,user_type):
		v = new_voidDataArray(len(data))
		for i in range(0,len(data) - 1):
			voidDataArray_setitem(v,i,data[i])  # Set a value
		stk_copy_chars_to_sequence(self._seq,v,len(data),user_type)
		delete_voidDataArray(v)
	def copy_string_to(self,data,user_type):
		return stk_copy_string_to_sequence(self._seq,data,user_type)
	@classmethod
	def find(cls,seqid):
		if seqid in cls.obj_map:
			return cls.obj_map[seqid]
