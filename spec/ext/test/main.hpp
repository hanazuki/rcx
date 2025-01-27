// SPDX-License-Identifier: BSL-1.0
// SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>
#pragma once

#include <rcx/rcx.hpp>

using namespace rcx::value;
using namespace rcx::typed_data;

inline rcx::ClassT<Exception> rb_eExpectationNotMetError() {
  return rcx::builtin::Object.const_get<Module>("RSpec")
      .const_get<Module>("Expectations")
      .const_get<ClassT<Exception>>("ExpectationNotMetError");
}

struct Test {
  static Value test_nil(Value self);
  static Value test_primitive(Value self);
  static Value test_string(Value self);
  static Value test_class(Value self);
  static Value test_ivar(Value self);
  static Value test_const(Value self);
  static Value test_singleton_method(Value self);
  static Value test_array(Value self);
  static Value test_leak(Value self);
  static Value test_allocate(Value self);
  static Value test_io_buffer(Value self);
  static Value test_format(Value self);
};

class Base: public WrappedStruct<> {
  std::string string_;
  int integer_;

public:
  Base(String string);
  void callback(Value callable) const;
  String string() const;
  void set_string(std::string_view s);
  virtual String virtual_1() const;
  void cxx_exception() const;
  void cxx_exception_unknown() const;
  void ruby_exception(Exception e) const;
  void ruby_exception_format(ClassT<Exception> e, String s) const;
  Value with_block(Value x, rcx::Proc block) const;
  Value with_block_opt(Value x, std::optional<rcx::Proc> block) const;
};

class Derived: public Base {
public:
  Derived(String string);
  String virtual_1() const override;
};

class Associated: public WrappedStruct<TwoWayAssociation> {
public:
  Associated &return_self();

  static std::tuple<Associated const &, Associated const &> swap(
      Value, std::tuple<Associated const &, Associated const &>);
};
