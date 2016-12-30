# Welcome to the Sequence Toolkit

This project is being open sourced from a private project. This is just the beginning of the open source efforts,
thus, not all your typical install instructions exist yet. What follows are the traditional instructions which
should work from the build dir of CLion or the root of an in-source build.

For building API bindings, see the README.md in the api directory - the following refers to the core library.

Its main development platform is a CLion project, but it can be build in-source using more traditional methods... 

## CLion

Load the project and build it... You probably know how already...

## In Source Building

To build the project in-source and without CLion, from the root dir:

```
 cmake .
 make
```



## Getting Started

Get started without installing, using the following examples as a guide:
	Depending on how you built STK, the library and binaries will be in different places. Set STKOBJDIR to the appropriate place, E.G.
		CLion: STKOBJDIR=$HOME/Library/Caches/CLion2016.*/cmake/generated/sequence_toolkit-*/*/Debug
		In source: STKOBJDIR=$PWD
	This will be used in the following examples to locate the STK library

	First, start the daemons. They can be run individually or as one threaded integrated daemon (as shown here).
		An example config file called stkd.cfg is provided in the daemons dir which uses localhost as the bind address.
		It is in the format '<named|httpd>: <args>' E.G. :
		
            httpd: -T 127.0.0.1:20001 -U 127.0.0.1:20001
            named: -T 127.0.0.1:20002 -U 127.0.0.1:20002

		cd daemons;
		LD_LIBRARY_PATH=$STKOBJDIR/lib ./stkd &
		cd ..
		
		Note, this is running the integrated daemon in the background.
		
		Visit http://localhost:8080/ in your browser

	Run the monitored service example (choose your language, examples is C):
	
		cd examples; LD_LIBRARY_PATH=$STKOBJDIR/lib ./monitored_service -t 2
		cd ruby_examples; LD_LIBRARY_PATH=$STKOBJDIR/lib ruby -I. monitored_service.rb -t 2 # make sure you gem install the gem first ;-)
		cd python_examples; LD_LIBRARY_PATH=$STKOBJDIR/lib:../site-packages-<python version> PYTHONPATH=../site-packages-<python version> python monitored_service.py -t 2
		cd java_examples; LD_LIBRARY_PATH=$STKOBJDIR/lib:../java $JAVA_HOME/bin/java -cp ../java/stk.jar:. monitored_service -t 2
	etc... Have fun!

	Also, here is a client/server example with name based monitoring:
	cd examples;
		# Registers itself as 'Simple Server Service Group' and searches for the web service by name 'monitor'
		LD_LIBRARY_PATH=$STKOBJDIR/lib ./simple_server -B 127.0.0.1:29312 -m lookup:monitor
		# Looks up 'Simple Server Service Group' and connects to it, searches for the web service by name 'monitor'
		LD_LIBRARY_PATH=$STKOBJDIR/lib ./simple_client -i 'lookup:Simple Server Service Group' -m lookup:monitor

Integrated Daemon vs Separate Daemons

	Its easier to manage one process than several, but if you need to run them separately,
	use the args in the std.cfg file as the command line args for each daemon. E.G.
		httpd: -T 127.0.0.1:20001
		named: -T 127.0.0.1:20002
	is the same as:
		cd daemons;
		LD_LIBRARY_PATH=$STKOBJDIR/lib ./stknamed -T 127.0.0.1:20002 &
		LD_LIBRARY_PATH=$STKOBJDIR/lib ./stkhttpd -T 127.0.0.1:20001 &

Full Installation Details:

Core Library Installation:
	Copy/move/extract this dir tree to your favoured location and ensure that your LD_LIBRARY_PATH includes the lib dir when running your application.

Programmers Reference:
	Please refer to the content of the doc dir

D3 (used in stkhttpd)

Copyright (c) 2013, Michael Bostock
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* The name Michael Bostock may not be used to endorse or promote products
  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL MICHAEL BOSTOCK BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
