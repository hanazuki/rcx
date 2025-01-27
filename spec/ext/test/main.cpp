// SPDX-License-Identifier: BSL-1.0
// SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>
#include "main.hpp"

#include <cmath>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>

#include <rcx/rcx.hpp>

#include "assert.hpp"

using namespace std::literals;
using namespace rcx::value;
using namespace rcx::literals;

static rcx::Leak<ClassT<Base>> cBase;
static rcx::Leak<ClassT<Derived>> cDerived;

Value Test::test_nil(Value self) {
  self.send("assert_nil", Value{});
  self.send("assert_nil", Value::qnil);
  self.send("assert_kind_of", rcx::builtin::Object, Value::qnil);

  return Value::qtrue;
}

Value Test::test_primitive(Value self) {
  auto roundtrip = [&](auto v, std::string s = "") {
    if(s.empty())
      s = std::format("{}", v);
    auto ruby_v = self.send("eval", String::copy_from(s));
    self.send("assert_equal", ruby_v, v);
    self.send("assert", v == self.send<decltype(v)>("eval", String::copy_from(s)));
  };

  roundtrip(true, "true");
  roundtrip(false, "false");

  roundtrip(std::numeric_limits<signed char>::min());
  roundtrip(std::numeric_limits<signed char>::max());
  roundtrip(std::numeric_limits<unsigned char>::max());
  roundtrip(std::numeric_limits<short>::min());
  roundtrip(std::numeric_limits<short>::max());
  roundtrip(std::numeric_limits<unsigned short>::max());
  roundtrip(std::numeric_limits<int>::min());
  roundtrip(std::numeric_limits<int>::max());
  roundtrip(std::numeric_limits<unsigned int>::max());
  roundtrip(std::numeric_limits<long>::min());
  roundtrip(std::numeric_limits<long>::max());
  roundtrip(std::numeric_limits<unsigned long>::max());
  roundtrip(std::numeric_limits<long long>::min());
  roundtrip(std::numeric_limits<long long>::max());
  roundtrip(std::numeric_limits<unsigned long long>::max());

  roundtrip(3459834.140625);
  roundtrip(std::numeric_limits<double>::infinity(), "Float::INFINITY");
  roundtrip(-std::numeric_limits<double>::infinity(), "-Float::INFINITY");
  roundtrip(std::numeric_limits<double>::epsilon(), "Float::EPSILON");
  roundtrip(-std::numeric_limits<double>::epsilon(), "-Float::EPSILON");

  self.send("assert_send", std::numeric_limits<double>::quiet_NaN(), "nan?"_sym);
  self.send("assert", std::isnan(self.send<double>("eval", "Float::NAN"_str)));

  self.send("assert_send", -0.0, "instance_eval"_str, "self.to_s == %{-0.0}"_str);
  self.send("assert", std::signbit(self.send<double>("eval", "-0.0"_str)) == true);

  return Value::qtrue;
}

Value Test::test_string(Value self) {
  auto const test = self.send("eval", u8"'test'"_str);
  auto const u8test = self.send("eval", u8"'テスト'"_fstr);
  auto const nulstr = self.send("eval", u8"\"test\\0test\""_str);

  {
    auto lit = "test"_str;
    self.send("assert_kind_of", rcx::builtin::String, lit);
    self.send("assert_equal", test, lit);
    self.send("assert_equal", 4, lit.size());
  }

  {
    auto copied = String::copy_from("test");
    self.send("assert_equal", test, copied);
    self.send("assert_not_predicate", copied, "frozen?"_sym);
  }

  {
    auto interned = String::intern_from("test");
    self.send("assert_equal", test, interned);
    self.send("assert_send", interned, "frozen?"_sym);
  }

  {
    auto copied = String::copy_from(u8"テスト");
    self.send("assert_equal", u8test, copied);
    self.send("assert_not_predicate", copied, "frozen?"_sym);
  }

  {
    auto interned = String::intern_from(u8"テスト");
    self.send("assert_equal", u8test, interned);
    self.send("assert_send", interned, "frozen?"_sym);
  }

  {
    self.send("assert_equal", nulstr, "test\0test"_str);
  }

  {
    auto flit = "test"_fstr;
    self.send("assert_kind_of", rcx::builtin::String, flit);
    self.send("assert_equal", test, flit);
    self.send("assert_send", "test"_fstr, "frozen?"_sym);
  }

  return Value::qtrue;
}

Value Test::test_class(Value self) {
  auto const m = Module::new_module();
  auto const c1 = Class::new_class();
  auto const c2 = Class::new_class(c1);

  self.send("assert_kind_of", rcx::builtin::Module, m);
  self.send("assert_kind_of", rcx::builtin::Class, c1);
  self.send("assert_kind_of", rcx::builtin::Class, c2);
  self.send("assert_send", c2, "<"_sym, c1);
  self.send("assert_send", c1, ">"_sym, c2);

  self.send("assert_equal", rcx::builtin::String, ""_str.get_class());
  self.send("assert_equal", rcx::builtin::Module, m.get_class());

  auto const cs = Class::new_class(rcx::builtin::String);
  String const s = cs.new_instance();
  self.send("assert_kind_of", rcx::builtin::String, s);

  return Value::qtrue;
}

Value Test::test_ivar(Value self) {
  using namespace rcx::arg;

  auto cls = Class::new_class()
                 .define_method(
                     "initialize", [](Value self, int s) -> void {}, arg<int>)
                 .define_method("foo",
                     [](Value self) -> int { return self.instance_variable_get<int>("foo"); });

  return Value::qtrue;
}

Value Test::test_const(Value self) {
  auto const m = Module::new_module();

  m.const_set("FOO", 1);
  ASSERT(m.const_defined("FOO"));
  ASSERT_NOT(m.const_defined("BAR"));
  self.send("assert_equal", 1, m.const_get("FOO"));
  ASSERT_EQ(1, m.const_get<int>("FOO"));

  return Value::qtrue;
}

Value Test::test_singleton_method(Value self) {
  using namespace rcx::arg;

  self.define_singleton_method(
      "m1",
      [&](Value self_, Symbol sym) {
        self.send("assert_same", self, self_);
        return sym;
      },
      arg<Symbol>);
  self.send("assert_equal", "ok"_sym, self.send("m1", "ok"_sym));

  self.define_singleton_method("m2", [](Value, int n) { return n * 3; }, arg<int>);
  self.send("assert_equal", 30, self.send("m2", 10));

  {
    auto str = "test"_str;
    str.define_singleton_method("foo", [](String s) { return s.send("*", 2); });
    self.send("assert_equal", "testtest"_str, str.send<String>("foo"));
  }

  return Value::qtrue;
}

Value Test::test_array([[maybe_unused]] Value self) {
  {
    auto const a = Array::new_array();
    ASSERT_EQ(0u, a.size());
  }

  {
    auto const a = Array::new_array(3);
    ASSERT_EQ(0u, a.size());
  }

  {
    std::array const vs{"a"_str, "b"_str};
    auto const a = Array::new_from(vs);
    ASSERT_EQ(2u, a.size());
  }

  {
    std::tuple const vs{"a"_str, "1"_sym};
    auto const a = Array::new_from(vs);
    ASSERT_EQ(2u, a.size());
  }

  {
    auto const a = Array::new_from({rcx::into_Value(1), rcx::into_Value(2)});
    ASSERT_EQ(2u, a.size());
    ASSERT_EQ(1, a.at<int>(0));
    ASSERT_EQ(1, a.at<int>(1));
  }

  {
    auto const a = Array::new_from({rcx::into_Value(1), rcx::into_Value(2)});
    a.push_back(5);
    ASSERT_EQ(3, a.size());
    ASSERT_EQ(5, a.pop_back<int>());
    ASSERT_EQ(2, a.size());

    a.push_front(6);
    ASSERT_EQ(3, a.size());
    ASSERT_EQ(6, a.pop_front<int>());
    ASSERT_EQ(2, a.size());
  }

  return Value::qtrue;
}

Value Test::test_leak([[maybe_unused]] Value self) {
  static rcx::Leak cls = Class::new_class();
  auto v = cls->new_instance();

  cls.clear();
  cls.set(Class::new_class());

  cls = Class::new_class();

  return Value::qtrue;
}

Value Test::test_allocate([[maybe_unused]] Value self) {

  auto v = cBase->allocate();
  DataType<Base>::initialize(v, "init"_str);

  return Value::qtrue;
}

Value Test::test_io_buffer([[maybe_unused]] Value self) {
  {
    auto b = IOBuffer::new_internal(40);
    auto s = b.bytes();
    ASSERT_EQ(40, s.size());
  }

  {
    auto b = IOBuffer::new_mapped(1'000'000);
    auto s = b.bytes();
    ASSERT_EQ(1'000'000, s.size());
  }

  {
    char a[100];
    auto b = IOBuffer::new_external(std::as_writable_bytes(std::span{a}));
    auto s = b.bytes();
    ASSERT_EQ(100, s.size());
    s[10] = std::byte{42};

    ASSERT_EQ(std::byte{42}, b.cbytes()[10]);

    b.free();
  }

  {
    char a[100];
    auto b = IOBuffer::new_external(std::as_bytes(std::span{a}));
    ASSERT_RAISE([&] { b.bytes(); });

    b.free();
  }

  {
    auto b = IOBuffer::new_internal(100);
    b.resize(200);
    ASSERT_EQ(200, b.bytes().size());

    std::scoped_lock lock(b);

    ASSERT_RAISE([&] { b.resize(300); });
  }

  {
    auto b1 = IOBuffer::new_internal(100);
    auto b2 = IOBuffer::new_internal(100);

    std::scoped_lock lock(b1, b2);
  }

  return Value::qtrue;
}

Value Test::test_format([[maybe_unused]] Value self) {
  {
    auto v = String::copy_from("test");
    ASSERT_EQ("<test>"sv, std::format("<{}>", v));
    ASSERT_EQ("<\"test\">"sv, std::format("<{:#}>", v));
  }

  return Value::qtrue;
}

Base::Base(String string): string_(std::string_view(string)) {
}

void Base::callback(Value callable) const {
  callable.send("call"_id);
}

String Base::string() const {
  return String::copy_from(string_);
}

void Base::set_string(std::string_view s) {
  string_ = s;
}

String Base::virtual_1() const {
  return String::copy_from("base");
}

void Base::cxx_exception() const {
  throw std::out_of_range{"pui"};
}

void Base::cxx_exception_unknown() const {
  throw 42;
}

void Base::ruby_exception(Exception e) const {
  throw e;
}

void Base::ruby_exception_format(ClassT<Exception> e, String s) const {
  throw rcx::Exception::format(e, "format {}", std::string_view(s));
}

Value Base::with_block(Value x, rcx::Proc block) const {
  return block.call(rcx::Array::new_from({
    x,
  }));
}

Derived::Derived(String string): Base(string) {
}

String Derived::virtual_1() const {
  return String::copy_from("derived");
}

Associated &Associated::return_self() {
  return *this;
}

std::tuple<Associated const &, Associated const &> Associated::swap(
    Value, std::tuple<Associated const &, Associated const &> arr) {
  return {std::ref(std::get<1>(arr)), std::ref(std::get<0>(arr))};
}

extern "C" void Init_test() {
  using namespace rcx::arg;

  auto &ruby = rcx::detail::ruby();

  [[maybe_unused]]
  auto mTest = ruby.define_module("Test")
                   .define_method("test_nil", &Test::test_nil)
                   .define_method("test_primitive", &Test::test_primitive)
                   .define_method("test_string", &Test::test_string)
                   .define_method("test_class", &Test::test_class)
                   .define_method("test_ivar", &Test::test_ivar)
                   .define_method("test_const", &Test::test_const)
                   .define_method("test_singleton_method", &Test::test_singleton_method)
                   .define_method("test_leak", &Test::test_leak)
                   .define_method("test_allocate", &Test::test_allocate)
                   .define_method("test_io_buffer", &Test::test_io_buffer)
                   .define_method("test_format", &Test::test_format);

  cBase = ruby.define_class<Base>("Base")
              .define_constructor(arg<String, "string">)
              .define_copy_constructor()
              .define_method_const("callback", &Base::callback, arg<Value, "callable">)
              .define_method_const("string", &Base::string)
              .define_method("string=", &Base::set_string, arg<std::string_view>)
              .define_method_const("virtual_1", &Base::virtual_1)
              .define_method_const("cxx_exception", &Base::cxx_exception)
              .define_method_const("cxx_exception_unknown", &Base::cxx_exception_unknown)
              .define_method_const("ruby_exception", &Base::ruby_exception, arg<Exception>)
              .define_method_const("ruby_exception_format", &Base::ruby_exception_format,
                  arg<ClassT<Exception>>, arg<String>)
              .define_method_const("with_block", &Base::with_block, arg<Value>, block);

  cDerived = ruby.define_class<Derived>("Derived", *cBase)
                 .define_copy_constructor()
                 .define_constructor(arg<String, "string">);

  [[maybe_unused]]
  auto cAssociated = ruby.define_class<Associated>("Associated")
                         .define_constructor()
                         .define_copy_constructor()
                         .define_method("return_self", &Associated::return_self)
                         .define_singleton_method("swap", &Associated::swap,
                             arg<std::tuple<Associated const &, Associated const &>>);
}
