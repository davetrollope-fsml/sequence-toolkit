
UNAME="$(uname)"
case "$UNAME" in
Darwin*)
	pythonver="2.7"
	PYTHONINCS=/usr/include/python${pythonver}/
	oldargs="-dynamiclib -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0"
	;;
*)
	if [ "$pythonver" == "" ]; then
		pythonver="2.4"
		PYTHONINCS=/usr/include/python${pythonver}/
	else
		PATH=$(echo /home/dave/Python-${pythonver}*):$PATH
		PYTHONINCS=$(echo /home/dave/Python-${pythonver}*/Include)
    fi
	oldargs="-shared"
	;;
esac

objs=""
if [ $# -eq 0 ]; then
	\rm -rf site-packages-$pythonver
	mkdir site-packages-$pythonver 2>&1 >/dev/null
	files=$(ls -1 ../include/*api.h | grep -v stk_sync | sort)
else
	files=""
	for i in $@
	do
		files="../include/${i}_api.h $files"
	done
fi

if [ "$BLDROOT" = "" ]; then
	export BLDROOT=$PWD/..
fi

for a in $files
do
	h=$(echo $a | sed "s/_api.h/.h/")
	m=$(basename $a | sed "s/_api.h//")
	i=$(basename $a | sed "s/_api.h/.i/")
	w=$(basename $a | sed "s/_api.h/_wrap.cxx/")
	o=$(basename $a | sed "s/_api.h/_wrap.o/")
	echo "Generating $m" >&2
	objs="$objs $o"
	case "$m" in
	stk_options)
		case "$UNAME" in
		Darwin*)
			;;
		*) ldargs="${oldargs} _stk_env.so"
			;;
		esac
		;;
	*)
		ldargs="${oldargs}"
	esac
	(case $m in
	stk_service_group)
		echo "
	%module(directors="1") $m
	 %{
	extern \"C\" {
	 #include \"../$h\"
	 #include \"../$a\"
	 #include \"../stk_service_cb.h\"
	}
	 %}
	"
		;;
	stk_env)
		echo "
	%module(directors="1") $m
	 %{
	extern \"C\" {
	 #include \"../$h\"
	 #include \"../$a\"
	 #include \"../stk_dispatcher_cb.h\"
	}
	 %}
	"
		;;
	*)
		if [ -r python_interface_helpers/$m.m ]; then
			eval echo "\"$(cat python_interface_helpers/$m.m)\""
		else
			eval echo "\"$(cat python_interface_helpers/default.m)\""
		fi
		;;
	esac

	if [ -r python_interface_helpers/$i ]; then
		cat python_interface_helpers/$i
	fi
	if [ -r common_interfaces/$m.c ]; then
		echo "%inline %{"
		cat common_interfaces/$m.c
		echo "%}"
	fi
	if [ -r python_interface_helpers/$m.py ]; then
		echo "%pythoncode %{"
		cat python_interface_helpers/$m.py
		echo "%}"
	fi

	echo "%include \"../../include/stk_env.h\"
	%include \"../../include/stk_common.h\"
	%include \"../$h\"
	%include \"../$a\""
	) >site-packages-$pythonver/$i

	cd site-packages-$pythonver >/dev/null
	swig -c++ -python -I.. $i
	gcc -g -fPIC -c $w -I../../include -I.. -I$PYTHONINCS -I$PYTHONINCS/..
	gcc -lstdc++ ../eg_dispatcher.o ${ldargs} $o -L$BLDROOT/lib -lstk -o _$m.so
	python -m compileall .
	#python -c "import py_compile;py_compile.compile(\"$p\")"
	cd ..
done

