Gem::Specification.new do |s|
  s.name        = 'stk'
  s.version     = '0.0.1'
  s.date        = '2015-04-09'
  s.summary     = "Sequence Toolkit APIs!"
  s.description = "Gem to package Sequence Toolkit APIs"
  s.authors     = ["Dave Trollope"]
  s.email       = 'dave@fsml-technologies.com'
  s.files       = [ "ruby_api/stk_data_flow.rb", "ruby_api/stk_env.rb", "ruby_api/stk_name_service.rb", "ruby_api/stk_options.rb",
					"ruby_api/stk_rawudp_client.rb", "ruby_api/stk_rawudp_listener.rb", "ruby_api/stk_sequence.rb", "ruby_api/stk_service.rb",
					"ruby_api/stk_service_group.rb", "ruby_api/stk_tcp_client.rb", "ruby_api/stk_tcp_server.rb", "ruby_api/stk_udp_client.rb",
					"ruby_api/stk_udp_listener.rb",

					"ruby/stkdata_flow_extconf.rb", "ruby/stkenv_extconf.rb", "ruby/stkname_service_extconf.rb", "ruby/stkoptions_extconf.rb",
					"ruby/stkrawudp_extconf.rb", "ruby/stksequence_extconf.rb", "ruby/stkservice_extconf.rb", "ruby/stkservice_group_extconf.rb",
					"ruby/stksg_automation_extconf.rb", "ruby/stksmartbeat_extconf.rb", "ruby/stktcp_client_extconf.rb", "ruby/stktcp_server_extconf.rb",
					"ruby/stktimer_extconf.rb", "ruby/stkudp_client_extconf.rb", "ruby/stkudp_listener_extconf.rb",

					"ruby/stkdata_flow.bundle", "ruby/stkenv.bundle", "ruby/stkname_service.bundle", "ruby/stkoptions.bundle", "ruby/stkrawudp.bundle",
					"ruby/stksequence.bundle", "ruby/stkservice.bundle", "ruby/stkservice_group.bundle", "ruby/stksg_automation.bundle",
					"ruby/stksmartbeat.bundle", "ruby/stktcp_client.bundle", "ruby/stktcp_server.bundle", "ruby/stktimer.bundle", "ruby/stkudp_client.bundle",
					"ruby/stkudp_listener.bundle" ]


  Gem::Specification.new "libstk", "1.0" do |s|
    s.extensions = %w[ext/libstk/extconf.rb]
  end

  s.homepage    =
    'http://rubygems.org/gems/stk'
  s.license       = 'Proprietary'
end

