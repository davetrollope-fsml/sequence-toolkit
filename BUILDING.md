## CLion

Load the project and build it... You probably know how already...

## In Source Building

To build the project in-source and without CLion, from the root dir:

```
 cmake .
 make
```

## Platform Support

The core STK library should build and run cleanly on both Linux and OS X.
However, little focus has been given to installing the library at the system level and thus there may be issues linking
against the library, E.G. with respect to OS X and recent changes in El Capitan.

CMake files have been tested on OS X Sierra and Centos 7

The APIs are not yet functional due to the above mentioned issues.

## Getting Started

Get started without installing, using the following examples as a guide:

	Depending on how you built STK, the library and binaries will be in different places. Set STKOBJDIR to the appropriate place, E.G.
		CLion 2016.2: STKOBJDIR=$HOME/Library/Caches/CLion2016.2/cmake/generated/sequence_toolkit-*/*/Debug
		CLion 2016.3: STKOBJDIR=$PWD/cmake-build-debug
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
		PYTHONVER=$(python --version 2>&1 | cut -f2 -d' ' |sed "s/\([0-9]\.[0-9]\).*/\1/")
		cd python_examples; LD_LIBRARY_PATH=$STKOBJDIR/lib:../site-packages-$PYTHONVER PYTHONPATH=../site-packages-$PYTHONVER python monitored_service.py -t 2
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
