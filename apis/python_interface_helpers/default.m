%module(directors="1") $m
%include "cstring.i"
%include "carrays.i"
%{
	extern \"C\" {
	#include \"../$h\"
	#include \"../$a\"
	}
%}

