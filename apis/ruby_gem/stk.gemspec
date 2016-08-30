# coding: utf-8
lib = File.expand_path('../lib', __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)
require 'stk/version'

Gem::Specification.new do |spec|
  spec.name          = "stk"
  spec.version       = Stk::VERSION
  spec.summary          = "Sequence Toolkit APIs!"
  spec.description      = "Gem to package Sequence Toolkit APIs"
  spec.authors          = ["Dave Trollope"]
  spec.email            = 'dave@fsml-technologies.com'
  spec.homepage         = 'http://rubygems.org/gems/stk'

  #spec.files         = `git ls-files -z`.split("\x0").reject { |f| f.match(%r{^(test|spec|features)/}) }
  #spec.files         = `ls -1 ../../ruby_api/*.rb ../../ruby/*.rb ../../ruby/*.bundle`.split("\x0").reject { |f| f.match(%r{^(test|spec|features)/}) }

  if RUBY_PLATFORM =~ /darwin*/
    ext="bundle"
  else
    ext="so"
  end

  spec.files         = [ "ruby_api/stk_data_flow.rb", "ruby_api/stk_env.rb", "ruby_api/stk_name_service.rb", "ruby_api/stk_options.rb",
                         "ruby_api/stk_rawudp_client.rb", "ruby_api/stk_rawudp_listener.rb", "ruby_api/stk_sequence.rb", "ruby_api/stk_service.rb",
                         "ruby_api/stk_service_group.rb", "ruby_api/stk_tcp_client.rb", "ruby_api/stk_tcp_server.rb", "ruby_api/stk_udp_client.rb",
                         "ruby_api/stk_udp_listener.rb",

                         "ruby/stkdata_flow/extconf.rb", "ruby/stkenv/extconf.rb", "ruby/stkname_service/extconf.rb", "ruby/stkoptions/extconf.rb",
                         "ruby/stkrawudp/extconf.rb", "ruby/stksequence/extconf.rb", "ruby/stkservice/extconf.rb", "ruby/stkservice_group/extconf.rb",
                         "ruby/stksg_automation/extconf.rb", "ruby/stksmartbeat/extconf.rb", "ruby/stktcp_client/extconf.rb", "ruby/stktcp_server/extconf.rb",
                         "ruby/stktimer/extconf.rb", "ruby/stkudp_client/extconf.rb", "ruby/stkudp_listener/extconf.rb",

                         "ruby/stkdata_flow.#{ext}", "ruby/stkenv.#{ext}", "ruby/stkname_service.#{ext}", "ruby/stkoptions.#{ext}", "ruby/stkrawudp.#{ext}",
                         "ruby/stksequence.#{ext}", "ruby/stkservice.#{ext}", "ruby/stkservice_group.#{ext}", "ruby/stksg_automation.#{ext}",
                         "ruby/stksmartbeat.#{ext}", "ruby/stktcp_client.#{ext}", "ruby/stktcp_server.#{ext}", "ruby/stktimer.#{ext}", "ruby/stkudp_client.#{ext}",
                         "ruby/stkudp_listener.#{ext}",

                         "lib/stk.rb", "lib/stk/version.rb" ]

  spec.platform      = Gem::Platform::CURRENT
  spec.bindir        = "exe"
  spec.executables   = spec.files.grep(%r{^exe/}) { |f| File.basename(f) }
  spec.require_paths = ["lib", "ruby", "ruby_api"]

  spec.licenses      = ["fsml"]

  if spec.respond_to?(:metadata)
    spec.metadata['allowed_push_host'] = "TODO: Set to 'http://mygemserver.com' to prevent pushes to rubygems.org, or delete to allow pushes to any server."
  end

  spec.add_development_dependency "bundler", "~> 1.9"
  spec.add_development_dependency "rake", "~> 10.0"
end
