require 'mkmf'
$CXXFLAGS << ' --std=c++20'
require 'rcx/mkmf'

$CXXFLAGS << ' -Werror=return-type'
$CXXFLAGS << ' -g3'

if checking_for("-MJ flag") { try_compile('', ' -MJtmp.json') }
  $CXXFLAGS << ' -MJ$@.json'
end

create_header
create_makefile('test')
