all: eg_dispatcher.o
	./mk_interfaces.sh # Builds in parallel

eg_dispatcher.o:
	cc -c -o eg_dispatcher.o -I ../include -I ../examples ../examples/eg_dispatcher.c

.PHONY: ruby python java

ruby: ruby_interface gem

ruby_interface:
	./mk_ruby_interfaces.sh

gem:
	./mk_gem.sh

java:
	./mk_java_interfaces.sh

python:
	./mk_python_interfaces.sh

