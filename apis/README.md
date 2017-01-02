# Welcome to the Sequence Toolkit APIs

The main project is built with CLion, but because of the multi-language nature of APIs, these are made with make and may
require customization of your environment. Also, since this is a newly open sourced project, not all your typical install
instructions and structure exist yet. What follows are the traditional instructions which should work from this dir as an in-source build.

## Dependencies

The API bindings are built using SWIG. You will need swig installed and in your path. How you do that, is up to you ;-) though you can modify the setenv_swig script which will be executed if swig is not in the path.

For the Python API, you will need the python-devel package installed so the python bindings can access headers etc.

## Building

Currently, the python and ruby APIs are working:

Run 'make python'
or
Run 'make ruby'

When building ruby, don't forget to install the gem that is built (follow provided instructions at the end of the build)!

## Getting Started

Make sure the core library is installed (or in the LD_LIBRARY_PATH), then:

	Python:
	Set the PYTHONPATH to include the site-packages dir

	Ruby:
	Locate the gem built (gem*/*.gem) for your version of ruby. Install the gem! gem install stk-*.gem

	Java:
	Do the same for the libraries in the java dir, then set the classpath to include the stk.jar

	And you should be able to run the examples. E.G.

        cd python_examples;
        LD_LIBRARY_PATH=../../lib:../site-packages-2.7 PYTHONPATH=../site-packages-2.7 python monitored_service.py -t 2

Programmers Reference:
	Please refer to the content of the doc dir

