#pragma once

#include <rcx/rcx.hpp>

using namespace rcx::value;
using namespace rcx::typed_data;

inline VALUE rb_eExpectationNotMetError() {
  static auto v = rcx::builtin::Object()
                      .const_get<Module>("RSpec")
                      .const_get<Module>("Expectations")
                      .const_get<Class>("ExpectationNotMetError");

  return v.as_VALUE();
}

struct Test {
  static Value test_nil(Value self);
  static Value test_primitive(Value self);
  static Value test_string(Value self);
  static Value test_class(Value self);
  static Value test_const(Value self);
  static Value test_singleton_method(Value self);
  static Value test_array(Value self);
  static Value test_pinning(Value self);
};

class Base : public WrappedStruct<> {
  std::string string_;
  int integer_;

public:
  Base(String string);
  String string() const;
  void set_string(std::string_view s);
  virtual String virtual_1() const;
  void cxx_exception() const;
  void cxx_exception_unknown() const;
  void ruby_exception(Value e) const;
};

class Derived : public Base {
public:
  Derived(String string);
  String virtual_1() const override;
};

class Associated : public WrappedStruct<TwoWayAssociation> {
public:
  Associated &return_self();
};
