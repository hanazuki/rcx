require 'rcx/mkmf'

$CXXFLAGS << ' --std=c++20'
$CXXFLAGS << ' -Werror'
$CXXFLAGS << ' -g3'

if checking_for("-MJ flag") { try_compile('', ' -MJtmp.json') }
  $CXXFLAGS << ' -MJ$@.json'
end

create_header
create_makefile('test')
