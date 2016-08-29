%module(directors="1") $m
%include "cstring.i"
%{
	extern \"C\" {
	#include \"../$h\"
	#include \"../$a\"
	#include \"../stk_dispatcher_cb.h\"
	}
%}

