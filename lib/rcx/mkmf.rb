# SPDX-License-Identifier: BSL-1.0
# SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>
require 'mkmf'
include MakeMakefile['C++']

root = File.join(__dir__, '../..')

$INCFLAGS << " -I#{File.join(root, 'include').shellescape}"

$rcx_headers = Dir[File.join(root, 'include/**/*.hpp')]

include (Module.new do
    def configuration(...)
      super.tap do |mk|
        mk << <<MAKEFILE
rcx_headers = #{$rcx_headers.join(?\s)}
ruby_headers := $(ruby_headers) $(rcx_headers)
MAKEFILE
      end
    end
  end)

## libffi

dir_config('libffi').any? || pkg_config('libffi')
ffi_h = 'ffi.h'
unless have_func('ffi_prep_cif', ffi_h)
  raise "libffi was not found"
end
unless have_func('ffi_closure_alloc', ffi_h) && have_func('ffi_prep_closure_loc', ffi_h)
  raise "libffi does not support closures"
end

if have_header('cxxabi.h')
  have_func('abi::__cxa_demangle', 'cxxabi.h')
  have_func('abi::__cxa_current_exception_type', 'cxxabi.h')
end

if checking_for("std::is_layout_compatible<>")  {
    try_compile(<<'CXX')
#include <type_traits>
struct A { int a; };
struct B { int b; };
static_assert(std::is_layout_compatible<A, B>::value);
CXX
  }
  $defs.push("-DHAVE_STD_IS_LAYOUT_COMPATIBLE=1")
end

if checking_for("nullability extension")  {
    try_compile("void *_Nullable p, *_Nonnull q;")
  }
  $defs.push("-DHAVE_FEATURE_NULLABILITY=1")
end

have_func('ruby_thread_has_gvl_p', 'ruby/thread.h')
