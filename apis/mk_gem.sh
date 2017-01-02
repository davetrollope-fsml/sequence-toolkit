rubyver=$(ruby -v | cut -f2 -d' ' |cut -f1 -dp)

gemdir="gem$rubyver"
[ -r $gemdir ] || mkdir $gemdir

cd $gemdir
[ -r ruby ] || ln -s ../ruby$rubyver ruby
[ -r ruby_api ] || ln -s ../ruby_api ruby_api
[ -r lib ] || ln -s ../ruby_gem/lib lib
[ -r stk.gemspec ] || ln -s ../ruby_gem/stk.gemspec stk.gemspec
gem build stk.gemspec
echo "Now install the gem :"
echo "cd $PWD"
echo "sudo gem install *.gem"
