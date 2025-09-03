#include <iostream>

#include <rcx/rcx.hpp>

void Init_examples_class() {
  using namespace rcx::args;

  auto ruby = rcx::Ruby::get();
  auto cls =
      ruby.define_class("Example")
          .define_method(
              "initialize",
              [](rcx::Value self, int foo) -> void { self.instance_variable_set("@foo", foo); },
              arg<int, "foo">)
          .define_method("foo",
              [](rcx::Value self) -> int { return self.instance_variable_get<int>("@foo"); });
  auto obj = cls.new_instance(42);

  std::cout << "obj.foo=" << obj.send<int>("foo") << std::endl;
}
