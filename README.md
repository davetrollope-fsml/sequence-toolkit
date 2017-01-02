# Welcome to the Sequence Toolkit

The Sequence Toolkit (STK) is a middleware product for applications running in the cloud. Add STK to your applications and you'll gain :

* Improved fault detection from Intelligent Heartbeating (Smartbeat Technology)
* Web based Application Monitoring for improved cloud monitoring.
* Visibility in to Application State (Starting/Running/Stopped/Timed Out/Sending/<Application defined>...)
* Ability to react to application state changes
* Granular Application Monitoring through Services.
* Monitoring of Individual Applications/Services or Groups
* Fast Pub/Sub Messaging
* Service Monitoring for Python, Ruby, Java and C/C++ Applications
* State Change Notification and Registration in Python and C/C++ Applications

STK simplifies implementing software/application services and eases application monitoring.

This project has been open sourced from a private project and thus not all your typical install instructions exist yet.
It is developed in CLion and with custom makefiles for the alternative APIs but can also be built in-source
using more traditional methods... 
For building API bindings, see the README.md in the api directory - the following refers to the core library.

What follows are the traditional instructions which should work from the build dir of CLion or the root of an in-source build.


# Building STK (Quickstart)

cmake CMakeLists.txt
make

By default, cmake generates libraries and binaries in the cmake-build-debug dir.

For more detailed instructions see [BUILDING.txt](BUILDING.md)

# Tutorial/Examples

All these examples assume you have setup the environment to find the STK library (using system library dirs or LD_LIBRARY_PATH's etc.)
and the STK daemon (stkd) is running (found in the lib and daemons directories of your build tree).

## C/C++

Make sure you cd to the examples dir!

### Service Monitoring, State Management

Run './monitored_service -t 2'

    Davids-MacBook-Air:examples dave$ ./monitored_service -t 2
    data flow 0x7fca85500480 name server data flow for monitored_service[29091] to port 20002 connected (fd 3)
    data flow 0x7fca85402880 monitoring socket for monitored_service[29090] to port 20001 connected (fd 6)
    Monitoring channel created
    Using random service ID: 37089166
    The new value of x is 2
    The new value of x is 4
    The new value of x is 8
    Monitoring channel destroyed
    Monitoring channel destroyed
    basic monitored service hit its threshold of 2
    
This shows a dummy application which calculated the value of 'x' which is connected to the STK daemon for monitoring.
Visiting http://localhost:8080 you will see:

![basic monitored service screenshot](images/basic_monitored_service.png)

This shows the basic service started and is now stopped and the Checkpoint was last at 2 - this was the threshold
specified with '-t 2' so the service clearly stopped as expected. :-)

### Pub/Sub

### Name Resolution, Named Meta-data Storage


# Legal Notices
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
