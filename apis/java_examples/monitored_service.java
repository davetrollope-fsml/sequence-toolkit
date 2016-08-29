
import java.util.*;
import java.lang.*;


public class monitored_service {
	static {
		System.loadLibrary("stk");
		System.loadLibrary("_stk_env");
		System.loadLibrary("_stk_options");
		System.loadLibrary("_stk_service");
	}

	static String service_name = "basic java monitored service";
	static int end_threshold = 1000000;

	private static void process_cmdline(String argv[]) {
		int i = 0;

		while(i < argv.length) {
			if(argv[i].equals("-t") && i < (argv.length-1)) {
				end_threshold = Integer.parseInt(argv[i + 1]);
				i += 1;
			}
			if(argv[i].equals("-S") && i < (argv.length-1)) {
				service_name = argv[i + 1];
				i += 1;
			}
			i += 1;
		}
	}

	public static void main(String argv[]) {
		process_cmdline(argv);

		String opts_str = "name_server_options\n" +
" data_flow_name tcp name server socket for simple_client\n" +
" data_flow_id 10000\n" +
" connect_address 127.0.0.1\n" +
" connect_port 20002\n" +
"monitoring_data_flow_options\n" +
" data_flow_name tcp monitoring socket for simple_client\n" +
" data_flow_id 10001\n" +
" connect_address 127.0.0.1\n" +
" connect_port 20001\n" +
" nodelay 1\n";

		// The formatted string of options are built in to an options table and then passed to stk_create_env
		stk_options_t envopts = stk_options.stk_build_options(opts_str,opts_str.length());
		stk_env stkbase = new stk_env(envopts);

		// Create our simple client service using a random ID, monitoring is inherited from the environment
		long svc_id = (new Random()).nextLong(); 

		stk_service svc = new stk_service(stkbase, service_name, svc_id, stk_service.STK_SERVICE_TYPE_DATA, null);

		// Set this service to a running state so folks know we are in good shape
		svc.set_state((short) stk_service.STK_SERVICE_STATE_RUNNING);

		// Start the application logic updating the checkpoint of the service and
		// invoke the timer handler to allow heartbeats to be sent
		long x = 1;
		long checkpoint = 0;
		while(true) {
			// Do some application logic
			x = x * 2;
			System.out.println("The new value of x is " + x);

			// Update this service's checkpoint so others know we are doing sane things
			svc.update_smartbeat_checkpoint(checkpoint);

			checkpoint += 1;

			stk_env.stk_env_dispatch_timer_pools(stkbase.ref(),0);

			// See if its time to end
			if(end_threshold > 0 && checkpoint > end_threshold)
				break;

			try {
				Thread.sleep(1000);
			} catch(Exception e) {
			}
		}

		// Set this service to state 'stopping' so folks know we are in ending
		svc.set_state((short) stk_service.STK_SERVICE_STATE_STOPPING);

		// Ok, now we can get rid of the service
		svc.close();

		// And get rid of the environment, we are done!
		stkbase.close();

		// Now free the options that were built
		// Because there was nested options, we must free each nest individually
		// because there is no stored indication which options are nested
		stk_options.stk_free_built_options(stk_options.find_sub_option(envopts,"monitoring_data_flow_options"));
		stk_options.stk_free_built_options(stk_options.find_sub_option(envopts,"name_server_options"));
		stk_options.stk_free_built_options(envopts);
		stk_options.stk_free_options(envopts);

		System.out.println(service_name + " hit its threshold of " + end_threshold);
	}
}

