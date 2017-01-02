
case "$(uname)" in
Darwin*)
	ldargs="-dynamiclib -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0"
	ext="dylib"
        #/Library/Java/JavaVirtualMachines/jdk1.7.0_45.jdk/Contents/Home/include and /System/Library/Frameworks/JavaVM.framework//Versions/A/Headers ?
        java_incflags="-I /Library/Java/JavaVirtualMachines/jdk1.7.0_45.jdk/Contents/Home/include"
	;;
*)
	ldargs="-shared"
	ext="so"
        incdir=$(ls -1dtr /usr/java/jdk*/include | tail -1)
        java_incflags="-I $incdir -I $incdir/linux"
	;;
esac
echo Using java headers in $java_incflags

if [ "$BLDROOT" = "" ]; then
	export BLDROOT=$PWD/..
fi

objs=""
\rm -rf java
mkdir java 2>&1 >/dev/null
for a in $(ls -1 ../include/*api.h | grep -v stk_sync)
do
h=$(echo $a | sed "s/_api.h/.h/")
m=$(basename $a | sed "s/_api.h//")
i=$(basename $a | sed "s/_api.h/.i/")
w=$(basename $a | sed "s/_api.h/_wrap.c/")
o=$(basename $a | sed "s/_api.h/_wrap.o/")
objs="$objs $o"
(echo "
%javaconst(1);
%module $m
 %{
 #include \"../$h\"
 #include \"../$a\"
 %}
"

case $m in
stk_env)
	echo "%pragma(java) modulecode=%{ 
private SWIGTYPE_p_stk_env_stct _env; public SWIGTYPE_p_stk_env_stct ref() { return _env; }
stk_env(stk_options_t options) { _env = stk_create_env(options); }
void close() { if(_env != null) { stk_destroy_env(_env); _env = null; } } %}"
	;;
stk_options)
	echo "%inline %{ stk_options_t *stk_void_to_options_t(void *options) { return options; } %}"

	echo "%pragma(java) modulecode=%{ 
static stk_options_t find_sub_option(stk_options_t options, String name) { SWIGTYPE_p_void subopts = stk_find_option(options,name,null); return stk_void_to_options_t(subopts); } %}"
	;;
stk_service)
	echo "%include \"../../include/stk_smartbeat.h\""

	echo "%inline %{ void stk_destroy_service_with_state(stk_service_t *svc,short state) {
		stk_service_state last_state = (stk_service_state) state;
		stk_destroy_service(svc,&last_state);
	} %}"

	echo "%pragma(java) modulecode=%{ 
private SWIGTYPE_p_stk_service_stct _svc; public SWIGTYPE_p_stk_service_stct ref() { return _svc; }
stk_service(stk_env env, String name, long id, int type, stk_options_t options)
{ _svc = stk_create_service(env.ref(), name, id, type, options); }
void close() { if(_svc != null) { stk_destroy_service(_svc,null); _svc = null; } }
void close(short last_state) { if(_svc != null) { stk_destroy_service_with_state(_svc,last_state); _svc = null; } }
void set_state(short state) { stk_service.stk_set_service_state(_svc, state); }
void update_smartbeat_checkpoint(long checkpoint) { stk_service.stk_service_update_smartbeat_checkpoint(_svc, checkpoint); } %}"
	;;
esac

echo "%include \"../../include/stk_env.h\"
%include \"../../include/stk_common.h\"
%include \"../$h\"
%include \"../$a\""
) >java/$i

cd java >/dev/null
swig -java $i
gcc -fPIC -c $w $java_incflags
gcc ${ldargs} $o -L$BLDROOT/lib -lstk -o lib_$m.$ext
cd ..

done
cd java
javac *.java
jar cf stk.jar *.class
cd ../java_examples
javac -cp ../java *.java

