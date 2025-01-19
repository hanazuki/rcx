# SPDX-License-Identifier: BSL-1.0
# SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>
require 'mkmf'
$CXXFLAGS << ' --std=c++20'
require 'rcx/mkmf'

$CXXFLAGS << ' -g3'

%w[
  -Werror=return-type
  -Werror=nullability-completeness
].each do |f|
  if checking_for("#{f} flag") { try_cflags(f) }
    $CXXFLAGS << " #{f}"
  end
end

if checking_for("-MJ flag") { try_compile('', ' -MJtmp.json') }
  $CXXFLAGS << ' -MJ$@.json'
end

create_header
create_makefile('test')
