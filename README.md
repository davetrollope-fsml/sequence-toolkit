# Welcome to the Sequence Toolkit

This project is being open sourced from a private project. This is just the beginning of the open source efforts,
thus, not all your typical install instructions exist yet. What follows are the traditional instructions which
should work from the build dir of CLion or the root of an in-source build, and the API bindings are not currently included... Coming...

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

	First, start the daemons. They can be run individually or as one threaded integrated daemon (as shown here).
		An example config file called stkd.cfg is provided in the daemons dir which uses localhost as the bind address.
		It is in the format '<named|httpd>: <args>' E.G. :
		
            httpd: -T 127.0.0.1:20001 -U 127.0.0.1:20001
            named: -T 127.0.0.1:20002 -U 127.0.0.1:20002

		cd daemons;
		LD_LIBRARY_PATH=../lib ./stkd &
		cd ..
		
		Note, this is running the integrated daemon in the background.

	Run the monitored service example (choose your language, examples is C):
	
		cd examples; LD_LIBRARY_PATH=../lib ./monitored_service -t 2
		cd ruby_examples; LD_LIBRARY_PATH=../lib ruby -I. monitored_service.rb -t 2 # make sure you gem install the gem first ;-)
		cd python_examples; LD_LIBRARY_PATH=../lib:../site-packages-<python version> PYTHONPATH=../site-packages-<python version> python monitored_service.py -t 2
		cd java_examples; LD_LIBRARY_PATH=../lib:../java $JAVA_HOME/bin/java -cp ../java/stk.jar:. monitored_service -t 2
	etc... Have fun!

	Also, here is a client/server example with name based monitoring:
	cd examples;
		# Registers itself as 'Simple Server Service Group' and searches for the web service by name 'monitor'
		LD_LIBRARY_PATH=../lib ./simple_server -B 127.0.0.1:29312 -m lookup:monitor
		# Looks up 'Simple Server Service Group' and connects to it, searches for the web service by name 'monitor'
		LD_LIBRARY_PATH=../lib ./simple_client -i 'lookup:Simple Server Service Group' -m lookup:monitor

Integrated Daemon vs Separate Daemons

	Its easier to manage one process than several, but if you need to run them separately,
	use the args in the std.cfg file as the command line args for each daemon. E.G.
		httpd: -T 127.0.0.1:20001
		named: -T 127.0.0.1:20002
	is the same as:
		cd daemons;
		LD_LIBRARY_PATH=../lib ./stknamed -T 127.0.0.1:20002 &
		LD_LIBRARY_PATH=../lib ./stkhttpd -T 127.0.0.1:20001 &

Full Installation Details:

Core Library Installation:
	Copy/move/extract this dir tree to your favoured location and ensure that your LD_LIBRARY_PATH includes the lib dir when running your application.

API Installation:
	Python:
	Install the Core Library, and set the PYTHONPATH to include the site-packages dir

	Ruby:
	Install the Core Library, and copy the content of the ruby dir to the ruby site path, eg. /usr/local/lib/ruby/site_ruby/2.0.0/x86_64-linux

	Java:
	Install the Core Library, and do the same for the libraries in the java dir, then set the classpath to include the stk.jar

Programmers Reference:
	Please refer to the content of the doc dir

Software License Agreement:

1. This is an agreement between Licensor (Dave Trollope) and Licensee, who is being licensed to use the named Software.

2. Licensee acknowledges that this is only a limited nonexclusive license. Licensor is and remains the owner of all titles, rights, and interests in the Software.

3. This License permits Licensee to install the Software on more than one computer system. Licensee will not make copies of the Software or allow copies of the Software to be made by others, unless authorized by this License Agreement. Licensee may make copies of the Software for backup purposes only.

4. This Software is subject to a limited warranty. Licensor warrants to Licensee that the Software will perform according to its documentation, and to the best of Licensor's knowledge Licensee's use of this Software according to the documentation is not an infringement of any third party's intellectual property rights. This limited warranty lasts for a period of 7 days after delivery. To the extent permitted by law, THE ABOVE-STATED LIMITED WARRANTY REPLACES ALL OTHER WARRANTIES, EXPRESS OR IMPLIED, AND LICENSOR DISCLAIMS ALL IMPLIED WARRANTIES INCLUDING ANY IMPLIED WARRANTY OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, OR OF FITNESS FOR A PARTICULAR PURPOSE. No agent of Licensor is authorized to make any other warranties or to modify this limited warranty. Any action for breach of this limited warranty must be commenced within one year of the expiration of the warranty. Because some jurisdictions do not allow any limit on the length of an implied warranty, the above limitation may not apply to this Licensee. If the law does not allow disclaimer of implied warranties, then any implied warranty is limited to 7 days after delivery of the Software to Licensee. Licensee has specific legal rights pursuant to this warranty and, depending on Licensee's jurisdiction, may have additional rights.

5. In case of a breach of the Limited Warranty, Licensee's exclusive remedy is as follows: Licensee will return all copies of the Software to Licensor, at Licensee's cost, along with proof of purchase. (Licensee can obtain a step-by-step explanation of this procedure, including a return authorization code, by contacting Licensor at http://www.sequence-toolkit.com) At Licensor's option, Licensor will either send Licensee a replacement copy of the Software, at Licensor's expense, or issue a full refund.

6. Notwithstanding the foregoing, LICENSOR IS NOT LIABLE TO LICENSEE FOR ANY DAMAGES, INCLUDING COMPENSATORY, SPECIAL, INCIDENTAL, EXEMPLARY, PUNITIVE, OR CONSEQUENTIAL DAMAGES, CONNECTED WITH OR RESULTING FROM THIS LICENSE AGREEMENT OR LICENSEE'S USE OF THIS SOFTWARE. Licensee's jurisdiction may not allow such a limitation of damages, so this limitation may not apply.

7. Licensee agrees to defend and indemnify Licensor and hold Licensor harmless from all claims, losses, damages, complaints, or expenses connected with or resulting from Licensee's business operations.

8. Licensor has the right to terminate this License Agreement and Licensee's right to use this Software upon any material breach by Licensee.

9. Licensee agrees to return to Licensor or to destroy all copies of the Software upon termination of the License.

10. This License Agreement is the entire and exclusive agreement between Licensor and Licensee regarding this Software. This License Agreement replaces and supersedes all prior negotiations, dealings, and agreements between Licensor and Licensee regarding this Software.

11. This License Agreement is governed by the law of Illinois applicable to Illinois contracts.

12. This License Agreement is valid without Licensor's signature. It becomes effective upon the earlier of Licensee's signature or Licensee's use of the Software.

Third Party Software License Agreements

Mongoose (used in stkhttpd):

Copyright (c) 2004-2010 Sergey Lyubka

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

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
