
objs=""

UNAME="$(uname)"

rubyver=$(ruby -v | cut -f2 -d' ' |cut -f1 -dp)
rubydir="ruby$rubyver"

if [ $# -eq 0 ]; then
	\rm -rf $rubydir
	[ -r $rubydir ] || mkdir $rubydir 2>&1 >/dev/null
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

# Generate all the files, then separately make them because
# the generation of the Makefile sucks in all C++ generated files which
# causes each file to be built multiple times. This is also why the
# option to build a single module is commented out above
for a in $files
do
	h=$(echo $a | sed "s/_api.h/.h/")
	c=$(basename $a | sed "s/_api.h//")
	m=$(basename $a | sed "s/_api.h//" | sed "s/_//")
	i=$(basename $a | sed "s/_api.h/.i/")
	w=$(basename $a | sed "s/_api.h/_wrap.c/")
	o=$(basename $a | sed "s/_api.h/_wrap.o/")
	mo=$(basename $a | sed "s/_api.h/.o/")
	echo "Generating $m" >&2
	objs="$objs $o"
	[ -r $rubydir/$m ] || mkdir $rubydir/$m
	(
	if [ -r ruby_api/$m.m ]; then
		eval echo "\"$(cat ruby_api/$m.m)\""
	else
		eval echo "\"$(cat ruby_api/default.m)\""
	fi

	case $m in
	stkservice)
		echo "%include \"../../include/stk_smartbeat.h\""
		;;
	esac

	if [ -r ruby_api/$i ]; then
		cat ruby_api/$i
	fi
	if [ -r common_interfaces/$c.c ]; then
		echo "%inline %{"
		cat common_interfaces/$c.c
		echo "%}"
	fi
	echo "%include \"../../include/stk_env.h\"
%include \"../../include/stk_common.h\"
%include \"../$h\"
%include \"../$a\""
	) >$rubydir/$m/$i

	cd $rubydir/$m >/dev/null
	swig -c++ -ruby -I.. -I../.. $i

	echo "require 'mkmf'
	create_makefile('$m')
	" >extconf.rb
	ruby extconf.rb

	case $(ruby -v | cut -f2 -d' ') in
	1*)
		# Ruby 1.8.7 on www
		make cflags="-I.. -I../../../include" ldflags="../../eg_dispatcher.o -L$BLDROOT/lib -lstk -lstdc++"
		;;
	2*)
		case "$UNAME" in
		Darwin*)
			# Ruby 2.0 on mac
			make CCDLFLAGS="-I.. -I../../../include" ARCH_FLAG="-arch x86_64" ldflags="../../eg_dispatcher.o -L$BLDROOT/lib -lstk"
			;;
		*)
			# Ruby 2.1.5 on www
			# -z muldefs for swig 3.0.5
			make CCDLFLAGS="-fPIC -I.. -I../../../include" ldflags="-z muldefs ../../eg_dispatcher.o -L$BLDROOT/lib -lstk -lstdc++"
			;;
		esac
		;;
	esac
	if [ -r $m.so ]; then cp $m.so ..; fi
	if [ -r $m.bundle ]; then cp $m.bundle ..; fi
	cd ../..
done
yardoc ruby_gem ruby_api -o ruby_doc --title "Sequence Toolkit Ruby API"

