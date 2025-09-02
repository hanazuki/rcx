# SPDX-License-Identifier: BSL-1.0
# SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>
require 'mkmf'
require_relative '../rcx'

module RCX
  CXX_STANDARD_FLAGS = {
    'c++20' => %w[--std=c++20 --std=c++2a].freeze,
    'c++23' => %w[--std=c++23 --std=c++2b].freeze,
    'c++26' => %w[--std=c++26 --std=c++2c].freeze,
  }.freeze

  root = File.join(__dir__, '../..')
  INCDIR = File.join(root, 'include').shellescape
  HEADERS = Dir[File.join(root, 'include/**/*.hpp')]

  module MakeMakefile
    include ::MakeMakefile['C++']

    def setup_rcx(cxx_standard: 'c++20')
      CXX_STANDARD_FLAGS.fetch(cxx_standard).find do |flag|
        if checking_for("whether #{flag} is accepted as CXXFLAGS") { try_cflags(flag) }
          $CXXFLAGS << " " << flag
          true
        else
          false
        end
      end or raise "C++ compiler does not support #{cxx_standard}"

      $INCFLAGS << " -I#{INCDIR}"

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
    end

    def configuration(...)
      super.tap do |mk|
        mk << <<MAKEFILE
rcx_headers = #{RCX::HEADERS.join(?\s)}
ruby_headers := $(ruby_headers) $(rcx_headers)
MAKEFILE
      end
    end
  end
end
