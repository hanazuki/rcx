// SPDX-License-Identifier: BSL-1.0
// SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>
#include "main.hpp"

#include <cerrno>
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
  using namespace rcx::args;

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
  using namespace rcx::args;

  auto obj = rcx::builtin::Object.new_instance();

  obj.define_singleton_method(
      "m1",
      [&](Value self_, Symbol sym) {
        self.send("assert_same", obj, self_);
        return sym;
      },
      arg<Symbol>);
  self.send("assert_equal", "ok"_sym, obj.send("m1", "ok"_sym));

  obj.define_singleton_method("m2", [](Value, int n) { return n * 3; }, arg<int>);
  self.send("assert_equal", 30, obj.send("m2", 10));

  {
    auto str = "test"_str;
    str.define_singleton_method("foo", [](String s) { return s.send("*", 2); });
    self.send("assert_equal", "testtest"_str, str.send<String>("foo"));
  }

  return Value::qtrue;
}

Value Test::test_singleton_method_without_self(Value self) {
  using namespace rcx::args;

  auto obj = rcx::builtin::Object.new_instance();

  obj.define_singleton_method<void>("m3", []() { return "hello"_str; });
  self.send("assert_equal", "hello"_str, obj.send("m3"));

  obj.define_singleton_method<void>("m4", [](int n) { return n * 4; }, arg<int>);
  self.send("assert_equal", 40, obj.send("m4", 10));

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

Value Test::test_args([[maybe_unused]] Value self) {
  using namespace rcx::args;
  auto cls = Class::new_class();
  cls.define_method(
      "args_splat",
      [self](Value, String str, Array ary) {
        self.send("assert_equal", "foo"_str, str);
        self.send("assert_equal", "bar"_str, ary[0]);
        self.send("assert_equal", "baz"_str, ary[1]);
        return 1;
      },
      arg<String>, arg_splat);

  self.send(
      "assert_equal", 1, cls.new_instance().send("args_splat", "foo"_str, "bar"_str, "baz"_str));

  return Value::qtrue;
}

Value Test::test_exception(Value self) {
  {
    auto exc = rcx::Exception::new_from_errno("test message", EAGAIN);
    self.send("assert_kind_of", rcx::builtin::SystemCallError, exc);
    self.send("assert_equal", EAGAIN, exc.send("errno"));
    self.send("assert_match", rcx::builtin::Regexp.new_instance("test message$"_str),
        exc.send("message"));
  }

  {
    errno = EAGAIN;
    auto exc = rcx::Exception::new_from_errno();
    self.send("assert_kind_of", rcx::builtin::SystemCallError, exc);
    self.send("assert_equal", EAGAIN, exc.send("errno"));
  }

  return Value::qtrue;
}

Value Test::test_io(Value self) {
  {
    auto io = self.send<IO>("eval"_sym, "File.open('/dev/null', 'w+')"_str);
    self.send("assert_kind_of"_sym, rcx::builtin::IO, io);
    self.send("assert_kind_of"_sym, rcx::builtin::Integer, io.descriptor());
    io.check_readable();
    io.check_writable();
  }

  {
    // Test when not readable
    auto io = self.send<IO>("eval"_sym, "File.open('/dev/null', 'w')"_str);
    ASSERT_RAISE([&] { io.check_readable(); });
  }

  {
    // Test when not writable
    auto io = self.send<IO>("eval"_sym, "File.open('/dev/null', 'r')"_str);
    ASSERT_RAISE([&] { io.check_writable(); });
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
  return block.call(rcx::Array::new_from({x}));
}

Value Base::with_block_opt(Value x, std::optional<Proc> block) const {
  if(block) {
    return block->call(rcx::Array::new_from({x}));
  } else {
    return x;
  }
}

Derived::Derived(String string): Base(string) {
}

String Derived::virtual_1() const {
  return String::copy_from("derived");
}

Associated &Associated::return_self() {
  return *this;
}

Value Test::test_optional(Value self) {
  // into_value
  {
    std::optional<int> v = 42;
    self.send("assert_equal", 42, rcx::into_Value(v));
  }
  {
    std::optional<int> v = std::nullopt;
    self.send("assert_nil", rcx::into_Value(v));
  }
  {
    std::optional<String> v = "hello"_str;
    self.send("assert_equal", "hello"_str, rcx::into_Value(v));
  }
  {
    std::optional<String> v = std::nullopt;
    self.send("assert_nil", rcx::into_Value(v));
  }

  // from_value
  {
    auto v = rcx::from_Value<std::optional<int>>(self.send("eval", "42"_str));
    ASSERT(v.has_value());
    ASSERT_EQ(42, *v);
  }
  {
    auto v = rcx::from_Value<std::optional<int>>(Value::qnil);
    ASSERT_NOT(v.has_value());
  }
  {
    auto v = rcx::from_Value<std::optional<String>>(self.send("eval", "'hello'"_str));
    ASSERT(v.has_value());
    self.send("assert_equal", "hello"_str, *v);
  }
  {
    auto v = rcx::from_Value<std::optional<String>>(Value::qnil);
    ASSERT_NOT(v.has_value());
  }
  {
    ASSERT_RAISE([&] { rcx::from_Value<std::optional<int>>("foo"_str); });
  }

  return Value::qtrue;
}

Value Test::test_gvl(Value self) {
  // Test basic functionality with void return type (returns bool)
  bool void_executed = rcx::gvl::without_gvl(
      []() {
        // This callback should execute successfully
      },
      rcx::gvl::ReleaseFlags::None);
  self.send("assert", void_executed);

  // Test with return value (returns std::optional<T>)
  auto result = rcx::gvl::without_gvl([]() { return 42; }, rcx::gvl::ReleaseFlags::None);
  self.send("assert", result.has_value());
  self.send("assert_equal", 42, result.value());

  // Test with flags using 2-parameter overload
  auto string_result = rcx::gvl::without_gvl(
      []() { return std::string("Hello from without GVL!"); }, rcx::gvl::ReleaseFlags::None);
  self.send("assert", string_result.has_value());
  self.send("assert_equal", String::copy_from("Hello from without GVL!"),
      String::copy_from(string_result.value()));

  // Test flag combinations using 2-parameter overload
  auto flag_result = rcx::gvl::without_gvl(
      []() { return 123; }, rcx::gvl::ReleaseFlags::IntrFail | rcx::gvl::ReleaseFlags::Offloadable);
  self.send("assert", flag_result.has_value());
  self.send("assert_equal", 123, flag_result.value());

  // Test with UBF
  bool ubf_called = false;
  auto ubf = [&ubf_called]() { ubf_called = true; };

  auto ubf_result = rcx::gvl::without_gvl([]() { return 456; }, ubf, rcx::gvl::ReleaseFlags::None);
  self.send("assert", ubf_result.has_value());
  self.send("assert_equal", 456, ubf_result.value());
  // Note: UBF may or may not be called depending on timing, so we don't assert on ubf_called

  // Test with both UBF and flags
  auto both_result = rcx::gvl::without_gvl(
      []() { return 789; }, []() { /* UBF function */ }, rcx::gvl::ReleaseFlags::Offloadable);
  self.send("assert", both_result.has_value());
  self.send("assert_equal", 789, both_result.value());

  return Value::qtrue;
}

std::tuple<Associated const &, Associated const &> Associated::swap(
    Value, std::tuple<Associated const &, Associated const &> arr) {
  return {std::ref(std::get<1>(arr)), std::ref(std::get<0>(arr))};
}

extern "C" void Init_test() {
  using namespace rcx::args;

  auto &ruby = rcx::Ruby::get();

  [[maybe_unused]]
  auto mTest = ruby.define_module("Test")
                   .define_method("test_nil", &Test::test_nil)
                   .define_method("test_primitive", &Test::test_primitive)
                   .define_method("test_string", &Test::test_string)
                   .define_method("test_class", &Test::test_class)
                   .define_method("test_ivar", &Test::test_ivar)
                   .define_method("test_const", &Test::test_const)
                   .define_method("test_singleton_method", &Test::test_singleton_method)
                   .define_method("test_singleton_method_without_self",
                       &Test::test_singleton_method_without_self)
                   .define_method("test_leak", &Test::test_leak)
                   .define_method("test_allocate", &Test::test_allocate)
                   .define_method("test_io_buffer", &Test::test_io_buffer)
                   .define_method("test_format", &Test::test_format)
                   .define_method("test_args", &Test::test_args)
                   .define_method("test_exception", &Test::test_exception)
                   .define_method("test_io", &Test::test_io)
                   .define_method("test_optional", &Test::test_optional)
                   .define_method("test_gvl", &Test::test_gvl);

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
              .define_method_const("with_block", &Base::with_block, arg<Value>, block)
              .define_method_const("with_block_opt", &Base::with_block_opt, arg<Value>, block_opt);

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
