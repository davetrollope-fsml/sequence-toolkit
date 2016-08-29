class stk_smartbeat:
	def __init__(self,smbref=None):
		self._smb = smbref
	def checkpoint(self):
		return stk_smartbeat_checkpoint(self._smb)

