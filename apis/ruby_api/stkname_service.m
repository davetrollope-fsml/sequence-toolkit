%module(directors="1") $m
%include "cstring.i"
%{
	extern \"C\" {
	#include \"../$h\"
	#include \"../$a\"
	#include \"../stk_name_service_cb.h\"
	}
%}

