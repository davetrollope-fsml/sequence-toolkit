PATH=$PATH:.
swig_installed=$(type -a swig; echo $?)
if [ $swig_installed -gt 0 ]; then
  . setenv_swig
fi

UNAME="$(uname)"
case "$UNAME" in
Darwin*)
	mk_python_interfaces.sh
	(mk_ruby_interfaces.sh; mk_gem.sh) &
	(PATH=$HOME/ruby216/bin:$PATH; mk_ruby_interfaces.sh; mk_gem.sh)
	(PATH=$HOME/ruby222/bin:$PATH; mk_ruby_interfaces.sh; mk_gem.sh)
	;;
*)
	./mk_python_interfaces.sh &
	pythonver=2.5 ./mk_python_interfaces.sh &
	pythonver=2.6 ./mk_python_interfaces.sh &
	pythonver=2.7 ./mk_python_interfaces.sh &
	(mk_ruby_interfaces.sh; mk_gem.sh) &
	(PATH=~/ruby215/bin:$PATH mk_ruby_interfaces.sh; mk_gem.sh) &
	;;
esac
mk_java_interfaces.sh &
wait

