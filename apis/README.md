# Welcome to the Sequence Toolkit APIs

The main project is build with CLion, but because of the multi-language nature of APIs, these are made with make and may
require customization on your environment. Also, since this is a newly open sourced project, not all your typical install
instructions and structure exist yet. What follows are the traditional instructions which should work from this dir as an in-source build.

## Building

The API bindings are built using SWIG. To build the project:

	1) customize/copy the setenv_swig script to setup your environment.

	2) Run 'make'

## Getting Started

Make sure the core library is installed, then:

	Python:
	Set the PYTHONPATH to include the site-packages dir

	Ruby:
	Copy the content of the ruby dir to the ruby site path, eg. /usr/local/lib/ruby/site_ruby/2.0.0/x86_64-linux

	Java:
	Do the same for the libraries in the java dir, then set the classpath to include the stk.jar

Programmers Reference:
	Please refer to the content of the doc dir

