%module(directors="1") $m
%include "cstring.i"
%{
	extern \"C\" {
	#include \"../$h\"
	#include \"../$a\"
	#include \"../stk_service_cb.h\"
	}
%}

