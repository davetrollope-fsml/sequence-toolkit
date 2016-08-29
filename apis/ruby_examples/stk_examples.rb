module Stk_examples
	def self.name_lookup_and_dispatch(stkbase,name,cb,opts,data,dispatcher_cbs,subscribe)
		if subscribe
			Stk_name_service.subscribe_to_name_info(stkbase,name,cb,stkbase,nil)
		else
			Stk_name_service.request_name_info(stkbase,name,1000,cb,stkbase,nil)
		end

		while (subscribe == false) && (data.cbs_rcvd < opts.cbs && data.expired == false) do
			begin
				stkbase.client_dispatcher_timed(dispatcher_cbs,1000);
				puts "Received #{data.cbs_rcvd} sequences, waiting for #{opts.cbs}"
			rescue StandardError => e
				puts "Exception occurred waiting for data to arrive: #{e.inspect}"
			end
		end
		if data.expired == 1 && data.cbs_rcvd == 1
			puts "Could not resolve #{name}"
			exit 5
		end
	end
end

