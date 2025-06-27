// SPDX-License-Identifier: BSL-1.0
// SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>
#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <format>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <ruby.h>
#include <ruby/encoding.h>
#include <ruby/io/buffer.h>

#define rcx_assert(expr) assert((expr))
#define rcx_delete(reason) delete

#ifdef HAVE_FEATURE_NULLABILITY
#define RCX_Nullable _Nullable
#define RCX_Nonnull _Nonnull
#else
#define RCX_Nullable
#define RCX_Nonnull
#endif

#if RUBY_IO_BUFFER_VERSION == 2
#define RCX_IO_BUFFER
#endif

/// RCX
///
namespace rcx {
  class Ruby;
  class Id;

  /// Value wrappers.
  ///
  namespace value {
    enum Nilability : bool;

    class ValueBase;
    template <typename Derived, std::derived_from<ValueBase> Super, Nilability> class ValueT;
    class Value;
    class Module;
    template <typename T = Value> class ClassT;
    using Class = ClassT<Value>;
    class Symbol;
    class Proc;
    class String;
    class Array;
    class Exception;
#ifdef RCX_IO_BUFFER
    class IOBuffer;
#endif
  }
  using namespace value;

  /// Concepts
  ///
  namespace concepts {
    template <typename T>
    concept StringLike = requires {
      typename std::remove_cvref_t<T>::value_type;
      typename std::remove_cvref_t<T>::traits_type;
      typename std::basic_string_view<typename std::remove_cvref_t<T>::value_type,
          typename std::remove_cvref_t<T>::traits_type>;
    };

    /// Specifies the types that can be used as Ruby identifiers.
    ///
    /// This includes \ref rcx::Id, \ref rcx::value::Symbol and C++ strings.
    template <typename T>
    concept Identifier = requires(T id) {
      { id.as_ID() } noexcept -> std::same_as<ID>;
    } || std::is_nothrow_constructible_v<Symbol, T>;
  }

  /// Implementation details.
  ///
  /// @internal
  namespace detail {
    template <typename T> struct unsafe_coerce {
      VALUE value;

      constexpr unsafe_coerce(VALUE value): value{value} {
      }

      template <std::derived_from<T> U>
      constexpr unsafe_coerce(unsafe_coerce<U> other): value{other.value} {
      }
    };

    template <typename T> inline constexpr bool always_false_v = false;

    // Duplicated definition for cxstring and u8cxstring instead of parameterize char type, because
    // clang-18 does not support template parameter deduction for type aliases.

    /// Represents a compile-time binary string.
    ///
    template <size_t N> struct cxstring {
      using value_type = char;
      using traits_type = std::char_traits<value_type>;

      std::array<value_type, N> data_;

      // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
      consteval cxstring(value_type const (&str)[N]) {
        std::copy_n(str, N, data_.data());
      }

      constexpr value_type const *RCX_Nonnull data() const {
        return data_.data();
      }

      constexpr size_t size() const {
        return N - 1;
      }

      constexpr operator std::basic_string_view<value_type>() const {
        return {data(), size()};
      }
    };

    /// Represents a compile-time UTF-8 string.
    ///
    template <size_t N> struct u8cxstring {
      using value_type = char8_t;
      using traits_type = std::char_traits<value_type>;

      std::array<value_type, N> data_;

      // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
      consteval u8cxstring(value_type const (&str)[N]) {
        std::copy_n(str, N, data_.data());
      }

      constexpr value_type const *RCX_Nonnull data() const {
        return data_.data();
      }

      constexpr size_t size() const {
        return N - 1;
      }

      constexpr operator std::basic_string_view<value_type>() const {
        return {data(), size()};
      }
    };

    using RbFunc = Value(std::span<Value> args, Value self);
    using NativeRbFunc = VALUE(int argc, VALUE *RCX_Nonnull argv, VALUE self);
    NativeRbFunc *RCX_Nonnull alloc_callback(std::function<RbFunc> f);

    struct Jump {
      int state;
    };

    template <std::invocable<> F>
    auto protect(F functor) -> auto
      requires(noexcept(functor()));
    template <typename A, typename R>
      requires(std::is_integral_v<A> && sizeof(A) == sizeof(VALUE) && std::is_integral_v<R> &&
               sizeof(R) == sizeof(VALUE))
    VALUE protect(VALUE (*RCX_Nonnull func)(A) noexcept, A arg);

    template <typename T> struct wrap_ref {
      using type = T;
    };
    template <typename U> struct wrap_ref<U &> {
      using type = std::reference_wrapper<U>;
    };
    template <typename T> using wrap_ref_t = wrap_ref<T>::type;
  }

  /// Conversion between C++ and Ruby values.
  ///
  namespace convert {
    /// Converts a C++ value into a Ruby value.
    ///
    /// @tparam T The type of the C++ value.
    /// @param value The C++ value to be converted.
    /// @return The converted Ruby value.
    template <typename T> Value into_Value(T value);
    /// Converts a Ruby value into a C++ value.
    ///
    /// @tparam T The type the C++ value.
    /// @param value The Ruby value to be converted.
    /// @return The converted C++ value.
    template <typename T> auto from_Value(Value value) -> auto;

    template <typename T> struct FromValue {
      static_assert(detail::always_false_v<T>, "conversion from Value not defined");
    };
    template <typename T> struct IntoValue {
      static_assert(detail::always_false_v<T>, "conversion into Value not defined");
    };

#define RCX_DECLARE_CONV(TYPE)                                                                     \
  template <> struct FromValue<TYPE> {                                                             \
    TYPE convert(Value value);                                                                     \
  };                                                                                               \
  template <> struct IntoValue<TYPE> {                                                             \
    Value convert(TYPE value);                                                                     \
  };

    RCX_DECLARE_CONV(bool);
    RCX_DECLARE_CONV(signed char);
    RCX_DECLARE_CONV(unsigned char);
    RCX_DECLARE_CONV(short);
    RCX_DECLARE_CONV(unsigned short);
    RCX_DECLARE_CONV(int);
    RCX_DECLARE_CONV(unsigned int);
    RCX_DECLARE_CONV(long);
    RCX_DECLARE_CONV(unsigned long);
    RCX_DECLARE_CONV(long long);
    RCX_DECLARE_CONV(unsigned long long);
    RCX_DECLARE_CONV(double);

#undef RCX_DECLARE_CONV
    template <> struct FromValue<std::string_view> {
      std::string_view convert(Value value);
    };

    template <> struct FromValue<Module> {
      Module convert(Value value);
    };

    template <> struct FromValue<Class> {
      Class convert(Value value);
    };
    template <> struct FromValue<Symbol> {
      Symbol convert(Value value);
    };
    template <> struct FromValue<Proc> {
      Proc convert(Value value);
    };
    template <> struct FromValue<String> {
      String convert(Value value);
    };
    template <> struct FromValue<Array> {
      Array convert(Value value);
    };
    template <> struct FromValue<Exception> {
      Exception convert(Value value);
    };
#ifdef RCX_IO_BUFFER
    template <> struct FromValue<IOBuffer> {
      IOBuffer convert(Value value);
    };
#endif

#define RCX_DECLARE_CLASS_CONV(CLS)                                                                \
  template <> struct FromValue<ClassT<CLS>> {                                                      \
    ClassT<CLS> convert(Value value);                                                              \
  };

    RCX_DECLARE_CLASS_CONV(Module);
    RCX_DECLARE_CLASS_CONV(Class);
    RCX_DECLARE_CLASS_CONV(Symbol);
    RCX_DECLARE_CLASS_CONV(Proc);
    RCX_DECLARE_CLASS_CONV(String);
    RCX_DECLARE_CLASS_CONV(Array);
    RCX_DECLARE_CLASS_CONV(Exception);
#ifdef RCX_IO_BUFFER
    RCX_DECLARE_CLASS_CONV(IOBuffer);
#endif

#undef RCX_DECLARE_CLASS_CONV
  }
  using namespace convert;

  namespace concepts {
    /// Specifies the types that can be converted from Ruby values.
    ///
    template <typename T>
    concept ConvertibleFromValue = requires(Value v) { from_Value<T>(v); };

    /// Specifies the types that can be converted into Ruby values.
    ///
    template <typename T>
    concept ConvertibleIntoValue = requires(T v) {
      { into_Value<T>(v) } -> std::same_as<Value>;
    };
  }

  namespace convert {
    template <concepts::ConvertibleFromValue T> struct FromValue<std::optional<T>> {
      decltype(auto) convert(Value v);
    };

    template <concepts::ConvertibleIntoValue T> struct IntoValue<std::optional<T>> {
      Value convert(std::optional<T> value);
    };

    template <concepts::ConvertibleFromValue... T> struct FromValue<std::tuple<T...>> {
      decltype(auto) convert(Value value);
    };

    template <concepts::ConvertibleIntoValue... T> struct IntoValue<std::tuple<T...>> {
      Value convert(std::tuple<T...> value);
    };
  }

  /// Argument parsing.
  ///
  namespace arg {
    template <concepts::ConvertibleFromValue T = Value> struct Self {
      using ResultType = detail::wrap_ref_t<T>;
      static ResultType parse(Ruby &, Value self, std::span<Value> &args);
    };

    template <concepts::ConvertibleFromValue T = Value, detail::cxstring name = ""> struct Arg {
      using ResultType = detail::wrap_ref_t<T>;
      static ResultType parse(Ruby &, Value self, std::span<Value> &args);
    };

    template <concepts::ConvertibleFromValue T = Value, detail::cxstring name = ""> struct ArgOpt {
      using ResultType = std::optional<detail::wrap_ref_t<T>>;
      static ResultType parse(Ruby &, Value self, std::span<Value> &args);
    };

    struct ArgSplat {
      using ResultType = Array;
      static ResultType parse(Ruby &, Value self, std::span<Value> &args);
    };

    struct Block {
      using ResultType = Proc;
      static ResultType parse(Ruby &, Value self, std::span<Value> &args);
    };

    struct BlockOpt {
      using ResultType = std::optional<Proc>;
      static ResultType parse(Ruby &, Value self, std::span<Value> &args);
    };

    /// The method receiver.
    ///
    /// The method accepts the method receiver and converts it into type T.
    template <concepts::ConvertibleFromValue T = Value> constexpr inline Self<T> self;
    /// A positional argument.
    ///
    template <concepts::ConvertibleFromValue T = Value, detail::cxstring name = "">
    constexpr inline Arg<T, name> arg;
    /// An optional positional argument.
    ///
    template <concepts::ConvertibleFromValue T = Value, detail::cxstring name = "">
    constexpr inline ArgOpt<T, name> arg_opt;
    /// The rest of the positional arguments.
    ///
    constexpr inline ArgSplat arg_splat;
    /// Block.
    constexpr inline Block block;
    /// Optional block.
    constexpr inline BlockOpt block_opt;
  }

  namespace concepts {
    /// Specifies the types that can be used as argument specifications.
    ///
    template <typename T>
    concept ArgSpec = requires(Ruby &ruby, Value self, std::span<Value> &args) {
      typename T::ResultType;
      { T::parse(ruby, self, args) } -> std::same_as<typename T::ResultType>;
    };
  }

  /// Maps C++ character types to Ruby encodings.
  ///
  template <typename CharT> struct CharTraits {
    static_assert(detail::always_false_v<CharT>, "Encoding unknown for this character type");
  };

  template <> struct CharTraits<char> {
    static inline constinit auto encoding = rb_ascii8bit_encoding;
  };
  template <> struct CharTraits<char8_t> {
    static inline constinit auto encoding = rb_utf8_encoding;
  };

  namespace concepts {
    template <typename T>
    concept CharTraits = requires {
      { T::encoding() } -> std::same_as<rb_encoding *>;
    };

    /// Specifies the character types that can be mapped to Ruby strings.
    ///
    template <typename T>
    concept CharLike = requires {
      requires CharTraits<::rcx::CharTraits<std::remove_cvref_t<T>>>;
      typename std::basic_string_view<std::remove_cvref_t<T>>;
    };
  };

  /// Literals
  ///
  /// This namespace contains C++ user-defined literals to generate Ruby objects.
  namespace literals {
    /// Creates a mutable `String` in ASCII-8BIT encoding.
    ///
    template <detail::cxstring> String operator""_str();
    /// Creates a mutable `String` in UTF-8 encoding.
    ///
    template <detail::u8cxstring> String operator""_str();
    /// Creates a frozen `String` in ASCII-8BIT encoding.
    ///
    template <detail::cxstring> String operator""_fstr();
    /// Creates a frozen `String` in UTF-8 encoding.
    ///
    template <detail::u8cxstring> String operator""_fstr();
    /// Creates a `Symbol` for the name encoded in ASCII/ASCII-8BIT.
    ///
    template <detail::cxstring> Symbol operator""_sym();
    /// Creates a `Symbol` for the name encoded in UTF-8.
    ///
    template <detail::u8cxstring> Symbol operator""_sym();
    /// Creates an ID for the name encoded in ASCII/ASCII-8BIT.
    ///
    /// IDs created this way is static and never garbage-collected.
    template <detail::cxstring> Id operator""_id();
    /// Creates an ID for the name encoded in UTF-8.
    ///
    /// IDs created this way is static and never garbage-collected.
    template <detail::u8cxstring> Id operator""_id();
  }

  /// Wrapper for static IDs.
  ///
  /// Static IDs are never garbage-collected, and it's safe to store anywhere.
  /// @sa rcx::literals::operator""_id()
  class Id {
    ID id_;

    explicit Id(ID id);

  public:
    Id(Id const &) = default;
    ID as_ID() const noexcept;

    template <detail::cxstring> friend Id literals::operator""_id();
    template <detail::u8cxstring> friend Id literals::operator""_id();
  };

  namespace value {
    /// Whether the value wrapper can be nil.
    enum Nilability : bool {
      Nonnil = false,
      Nilable = true,
    };

    /// Base class for all value wrappers.
    ///
    /// Use \ref rcx::value::Value instead of this class.
    class ValueBase {
      VALUE value_;

    protected:
      /// Constructs a `ValueBase` from a Ruby value.
      ///
      /// @param value The Ruby value to be wrapped.
      constexpr ValueBase(VALUE value);

    public:
      /// Constructs a `ValueBase` from a nil value.
      constexpr ValueBase();
      /// Unwraps the `VALUE`.
      ///
      /// @return The wrapped Ruby `VALUE`.
      /// @warning This method should be used with caution and only when you have to call Ruby API
      /// directly.
      constexpr VALUE as_VALUE() const;
      /// Constructs a `ValueBase` from a coerced value.
      ///
      /// @param coerce The coerced value.
      /// @warning This constructor is unsafe.
      ValueBase(detail::unsafe_coerce<ValueBase> coerce): value_(coerce.value) {
      }

      /// Checks if the wrapped value is nil.
      ///
      /// @return Whether the value is nil.
      bool is_nil() const;
      /// Checks if the wrapped value is frozen.
      ///
      /// @return Whether the value is frozen.
      bool is_frozen() const;
      /// Checks if the wrapped value is an instance of a class.
      ///
      /// @param klass The class to check against.
      /// @return Whether the value is an instance of the class.
      template <typename T> bool is_instance_of(ClassT<T> klass) const;
      /// Checks if the wrapped value is a kind of a class.
      ///
      /// @param klass The class to check against.
      /// @return Whether the value is a kind of the class.
      template <typename T> bool is_kind_of(ClassT<T> klass) const;
    };

    template <typename Derived, std::derived_from<ValueBase> Super, Nilability nilable = Nonnil>
    class ValueT: public Super {
    public:
      constexpr ValueT()
        requires(nilable == Nilability::Nilable)
      = default;
      constexpr ValueT()
        requires(nilable != Nilability::Nilable)
      = rcx_delete("This type of Value cannot be nil.");

      template <std::derived_from<Derived> T> ValueT(T const &value): Super(value.as_VALUE()) {
      }
      constexpr ValueT(detail::unsafe_coerce<Derived> coerce): Super(coerce) {
      }
      Derived &operator=(Derived const &other) {
        Super::operator=(other);
        return *this;
      }

      ClassT<Derived> get_class() const;
      Derived freeze() const;

      template <concepts::ConvertibleFromValue Self = Derived, concepts::ArgSpec... ArgSpec>
      Derived define_singleton_method(concepts::Identifier auto &&mid,
          std::invocable<Self, typename ArgSpec::ResultType...> auto &&function,
          ArgSpec... argspec) const;
    };

    class Value: public ValueT<Value, ValueBase, Nilable> {
    public:
      using ValueT<Value, ValueBase, Nilable>::ValueT;

      template <concepts::ConvertibleFromValue R = Value>
      R send(concepts::Identifier auto &&mid, concepts::ConvertibleIntoValue auto &&...args) const;

      bool test() const noexcept;

      /// Converts the object into a String using its `#inspect` method.
      ///
      /// @return The converted string.
      String inspect() const;

      /// Converts the object into a String using its `#to_s` method.
      ///
      /// @return The converted string.
      String to_string() const;

      bool instance_variable_defined(concepts::Identifier auto &&name) const;
      template <concepts::ConvertibleFromValue T = Value>
      auto instance_variable_get(concepts::Identifier auto &&name) const -> auto;
      void instance_variable_set(
          concepts::Identifier auto &&name, concepts::ConvertibleIntoValue auto &&value) const;

      static Value const qnil;
      static Value const qtrue;
      static Value const qfalse;
      static Value const qundef;
    };

    /// Represents a Ruby module or a class.
    ///
    class Module: public ValueT<Module, Value> {
    public:
      using ValueT<Module, Value>::ValueT;

      /// Returns the name path of this module.
      ///
      /// @return The name path of this module.
      String name() const;

      /// Defines a module under this module.
      ///
      /// @warning Modules defined this way will be never garbage-collected.
      ///
      /// @param name Name of the module.
      /// @return The newly defined module.
      Module define_module(concepts::Identifier auto &&name) const;

      /// Defines a class under this module.
      ///
      /// @warning Classes defined this way will be never garbage-collected.
      ///
      /// @param name Name of the class.
      /// @param superclass The new class will be a subclass of this class.
      /// @return The newly defined class.
      template <typename T = Value, typename S>
      ClassT<T> define_class(concepts::Identifier auto &&name, ClassT<S> superclass) const;

      /// Defines a subclass of Object under this module.
      ///
      /// @warning Classes defined this way will be never garbage-collected.
      ///
      /// @param name Name of the class.
      /// @return The newly created class.
      template <typename T = Value> ClassT<T> define_class(concepts::Identifier auto &&name) const;

      /// Defines an instance method.
      ///
      /// @warning Defining method this way allocates a resource that will never be
      /// garbage-collected.
      ///
      /// @tparam Self The type of self.
      /// @param mid The name of the method.
      /// @param function The function to be called.
      /// @param argspec List of argument specifications.
      /// @return Self.
      template <concepts::ConvertibleFromValue Self = Value, concepts::ArgSpec... ArgSpec>
      Module define_method(concepts::Identifier auto &&mid,
          std::invocable<Self, typename ArgSpec::ResultType...> auto &&function,
          ArgSpec... argspec) const;

      /// Checks if a constant is defined under this module.
      ///
      /// @param name Name of the constant.
      /// @returns Whether the constant is defined.
      bool const_defined(concepts::Identifier auto &&name) const;

      /// Gets the value of a constant under this module.
      ///
      /// @tparam T The type the constant value should be converted into.
      /// @param name Name of the constant.
      /// @return The value converted into T.
      template <concepts::ConvertibleFromValue T = Value>
      T const_get(concepts::Identifier auto &&name) const;

      /// Defines a constant with a value under this module.
      ///
      /// @param name The name of the constant.
      /// @param value The value to be set.
      void const_set(
          concepts::Identifier auto &&name, concepts::ConvertibleIntoValue auto &&value) const;

      /// Creates an anonymous module.
      ///
      /// @return The newly created module.
      static Module new_module();
    };

    template <typename T>
    class [[clang::preferred_name(Class)]] ClassT: public ValueT<ClassT<T>, Module> {
    public:
      using ValueT<ClassT<T>, Module>::ValueT;

      /// Allocates and initializes an instance of this class.
      ///
      /// @param args The arguments to be passed to `initialize.
      /// @return The new instance.
      T new_instance(concepts::ConvertibleIntoValue auto &&...args) const
        requires std::derived_from<T, ValueBase>;

      /// Allocates an uninitialized instance of this class.
      ///
      /// @returns The newly allocated uninitialized object.
      Value allocate() const;

      /// Checks if this class is a subclass of another class.
      ///
      /// @param klass The class to check against.
      /// @return Whether this class is a subclass of the given class.
      template <typename S> bool is_subclass_of(ClassT<S> klass) const;
      /// Checks if this class is a superclass of another class.
      ///
      /// @param klass The class to check against.
      /// @return Whether this class is a superclass of the given class.
      template <typename S> bool is_superclass_of(ClassT<S> klass) const;

      /// Defines a mutating instance method.
      ///
      /// The method will raise a `FrozenError` if the object is frozen.
      /// @param mid The name of the method.
      /// @param function The function to be called.
      /// @param argspec List of argument specifications.
      /// @return Self.
      /// @warning Defining method this way allocates a resource that will never be
      /// garbage-collected.
      template <concepts::ArgSpec... ArgSpec>
      ClassT<T> define_method(concepts::Identifier auto &&mid,
          std::invocable<T &, typename ArgSpec::ResultType...> auto &&function,
          ArgSpec... argspec) const;

      /// Defines a non-mutating instance method.
      ///
      /// The method can be called even when the object is frozen.
      /// @param mid The name of the method.
      /// @param function The function to be called.
      /// @param argspec List of argument specifications.
      /// @return Self.
      /// @warning Defining method this way allocates a resource that will never be
      /// garbage-collected.
      template <concepts::ArgSpec... ArgSpec>
      ClassT<T> define_method_const(concepts::Identifier auto &&mid,
          std::invocable<T const &, typename ArgSpec::ResultType...> auto &&function,
          ArgSpec... argspec) const;

      /// Defines `initialize` method using a C++ constructor.
      ///
      /// @param argspec List of argument specifications.
      /// @return Self.
      template <concepts::ArgSpec... ArgSpec>
        requires std::constructible_from<T, typename ArgSpec::ResultType...>
      ClassT<T> define_constructor(ArgSpec... argspec) const;

      /// Defines `initialize_copy` method using the C++ copy constructor.
      ///
      /// @return Self.
      ClassT<T> define_copy_constructor() const
        requires std::copy_constructible<T>;

      /// Creates a new class.
      ///
      /// @return The newly created class.
      static Class new_class();

      /// Creates a new class with a superclass.
      ///
      /// @param superclass The new class will be a subclass of this class.
      /// @return The newly created class.
      template <typename S> static ClassT<S> new_class(ClassT<S> superclass);
    };

    class Symbol: public ValueT<Symbol, Value> {
    public:
      using ValueT<Symbol, Value>::ValueT;
      template <size_t N> explicit Symbol(char const (&)[N]) noexcept;
      explicit Symbol(std::string_view sv) noexcept;

      /**
       * Returns Ruby-internal ID.
       *
       * The ID returned by this method may be dynamic and subject to garbage collection.
       * So do not store, whether on stack or in heap.
       */
      ID as_ID() const noexcept;
    };

    class String: public ValueT<String, Value> {
    public:
      using ValueT<String, Value>::ValueT;

      template <concepts::StringLike S> static String intern_from(S &&s);
      template <concepts::CharLike CharT> static String intern_from(CharT const *RCX_Nonnull s);
      template <concepts::StringLike S> static String copy_from(S &&s);
      template <concepts::CharLike CharT> static String copy_from(CharT const *RCX_Nonnull s);

      size_t size() const noexcept;
      char *RCX_Nonnull data() const;
      char const *RCX_Nonnull cdata() const noexcept;
      explicit operator std::string_view() const noexcept;

      String lock() const;
      String unlock() const;
    };

    class Array: public ValueT<Array, Value> {
    public:
      using ValueT<Array, Value>::ValueT;

      size_t size() const noexcept;
      template <concepts::ConvertibleFromValue T = Value> decltype(auto) at(size_t i) const;
      Value operator[](size_t i) const;

      template <std::ranges::contiguous_range R>
#ifdef HAVE_STD_IS_LAYOUT_COMPATIBLE
        requires std::is_layout_compatible_v<std::ranges::range_value_t<R>, ValueBase>
#else
        requires(std::derived_from<std::ranges::range_value_t<R>, ValueBase> &&
                 sizeof(std::ranges::range_value_t<R>) == sizeof(ValueBase))
#endif
      static Array new_from(R const &elements);

      static Array new_from(std::initializer_list<ValueBase> elements);

      template <std::derived_from<ValueBase>... T>
      static Array new_from(std::tuple<T...> const &elements);

      static Array new_array();
      static Array new_array(long capacity);

      template <concepts::ConvertibleIntoValue T = Value> Array push_back(T value) const;
      template <concepts::ConvertibleFromValue T = Value> T pop_back() const;
      template <concepts::ConvertibleIntoValue T = Value> Array push_front(T value) const;
      template <concepts::ConvertibleFromValue T = Value> T pop_front() const;
    };

    class Proc: public ValueT<Proc, Value> {
    public:
      using ValueT<Proc, Value>::ValueT;

      bool is_lambda() const;
      Value call(Array args) const;
    };

    class Exception: public ValueT<Exception, Value> {
    public:
      using ValueT<Exception, Value>::ValueT;

      template <std::derived_from<Exception> E, typename... Args>
      static E format(ClassT<E> cls, std::format_string<Args...> fmt, Args &&...args);
      static Exception new_from_errno(char const *RCX_Nonnull message, int err = errno);
    };

#ifdef RCX_IO_BUFFER
    /// Represents an `IO::Buffer` object.
    class IOBuffer: public ValueT<IOBuffer, Value> {
    public:
      using ValueT<IOBuffer, Value>::ValueT;

      /// Creates an `IO::Buffer` with internal storage.
      ///
      /// @param size The size of the buffer.
      /// @return The newly created buffer.
      static IOBuffer new_internal(size_t size);
      /// Creates an `IO::Buffer` with mapped storage.
      ///
      /// @param size The size of the buffer.
      /// @return The newly created buffer.
      static IOBuffer new_mapped(size_t size);
      /// Creates an `IO::Buffer` with externally managed storage. The returned `IO::Buffer` should
      /// be `free`d when the underlying storage is longer valid.
      ///
      /// @param bytes The contiguous memory region to be used as the storage.
      /// @return The newly created buffer.
      template <size_t N = std::dynamic_extent>
      static IOBuffer new_external(std::span<std::byte, N> bytes [[clang::lifetimebound]]);
      /// Creates an `IO::Buffer` with externally managed read-only storage. The returned
      /// `IO::Buffer` should be `free`d when the underlying storage is longer valid.
      ///
      /// @param bytes The contiguous memory region to be used as the storage.
      /// @return The newly created buffer.
      template <size_t N = std::dynamic_extent>
      static IOBuffer new_external(std::span<std::byte const, N> bytes [[clang::lifetimebound]]);

      /// Frees the internal storage or disassociates the external storage.
      ///
      /// See the Ruby documentation for `IO::Buffer#free`.
      void free() const;
      /// Resizes the `IO::Buffer` to the given size.
      ///
      /// @param size The new size of the buffer.
      void resize(size_t size) const;

      /// Returns the bytes of the `IO::Buffer`. This will raise if the `IO::Buffer` is not
      /// writable.
      ///
      /// @return The bytes of the `IO::Buffer`.
      std::span<std::byte> bytes() const;
      /// Returns the bytes of the `IO::Buffer` as a read-only span.
      ///
      /// @return The bytes of the `IO::Buffer`.
      std::span<std::byte const> cbytes() const;

      // BasicLockable
      /// Locks the `IO::Buffer`.
      ///
      void lock() const;
      /// Unlocks the `IO::Buffer`.
      ///
      void unlock() const;

      // Lockable
      /// Tries to lock the `IO::Buffer`.
      ///
      /// @return Whether the lock is successful.
      bool try_lock() const;
    };
#endif
  }
  /// Built-in classes.
  ///
  namespace builtin {
    /// `NilClass` class
    ///
    inline value::Class const NilClass = detail::unsafe_coerce<value::Class>(::rb_cNilClass);
    /// `TrueClass` class
    ///
    inline value::Class const TrueClass = detail::unsafe_coerce<value::Class>(::rb_cTrueClass);
    /// `FalseClass` class
    ///
    inline value::Class const FalseClass = detail::unsafe_coerce<value::Class>(::rb_cFalseClass);
    /// `Class` class
    ///
    inline ClassT<value::Class> const Class =
        detail::unsafe_coerce<ClassT<value::Class>>(::rb_cClass);
    /// `Module` class
    ///
    inline ClassT<value::Module> const Module =
        detail::unsafe_coerce<ClassT<value::Module>>(::rb_cModule);
    /// `BasicObject` class
    ///
    inline value::Class const BasicObject = detail::unsafe_coerce<value::Class>(::rb_cBasicObject);
    /// `Object` class
    ///
    inline value::Class const Object = detail::unsafe_coerce<value::Class>(::rb_cObject);
    /// `String` class
    ///
    inline ClassT<value::String> const String =
        detail::unsafe_coerce<ClassT<value::String>>(::rb_cString);
    /// `Encoding` class
    ///
    inline value::Class const Encoding = detail::unsafe_coerce<value::Class>(::rb_cEncoding);
    /// `Symbol` class
    ///
    inline value::Class const Symbol = detail::unsafe_coerce<value::Class>(::rb_cSymbol);
    /// `Regexp` class
    ///
    inline value::Class const Regexp = detail::unsafe_coerce<value::Class>(::rb_cRegexp);
    /// `MatchData` class
    ///
    inline value::Class const MatchData = detail::unsafe_coerce<value::Class>(::rb_cMatch);
    /// `Array` class
    ///
    inline ClassT<value::Array> const Array =
        detail::unsafe_coerce<ClassT<value::Array>>(::rb_cArray);
    /// `Hash` class
    ///
    inline value::Class const Hash = detail::unsafe_coerce<value::Class>(::rb_cHash);
    /// `Proc` class
    ///
    inline ClassT<value::Proc> const Proc = detail::unsafe_coerce<ClassT<value::Proc>>(::rb_cProc);
    /// `Method` class
    ///
    inline value::Class const Method = detail::unsafe_coerce<value::Class>(::rb_cMethod);
    /// `Numeric` class
    ///
    inline value::Class const Numeric = detail::unsafe_coerce<value::Class>(::rb_cNumeric);
    /// `Integer` class
    ///
    inline value::Class const Integer = detail::unsafe_coerce<value::Class>(::rb_cInteger);
    /// `Float` class
    ///
    inline value::Class const Float = detail::unsafe_coerce<value::Class>(::rb_cFloat);
    /// `Rational` class
    ///
    inline value::Class const Rational = detail::unsafe_coerce<value::Class>(::rb_cRational);
    /// `Complex` class
    ///
    inline value::Class const Complex = detail::unsafe_coerce<value::Class>(::rb_cComplex);
    /// `Range` class
    ///
    inline value::Class const Range = detail::unsafe_coerce<value::Class>(::rb_cRange);
    /// `IO` class
    ///
    inline value::Class const IO = detail::unsafe_coerce<value::Class>(::rb_cIO);
    /// `File` class
    ///
    inline value::Class const File = detail::unsafe_coerce<value::Class>(::rb_cFile);
    /// `Thread` class
    ///
    inline value::Class const Thread = detail::unsafe_coerce<value::Class>(::rb_cThread);

    /// `Exception` class
    ///
    inline value::ClassT<value::Exception> const Exception =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eException);
    /// `StandardError` class
    ///
    inline value::ClassT<value::Exception> const StandardError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eStandardError);
    /// `SystemExit` class
    ///
    inline value::ClassT<value::Exception> const SystemExit =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eSystemExit);
    /// `Interrupt` class
    ///
    inline value::ClassT<value::Exception> const Interrupt =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eInterrupt);
    /// `SignalException` class
    ///
    inline value::ClassT<value::Exception> const SignalException =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eSignal);
    /// `ArgumentError` class
    ///
    inline value::ClassT<value::Exception> const ArgumentError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eArgError);
    /// `EOFError` class
    ///
    inline value::ClassT<value::Exception> const EOFError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eEOFError);
    /// `IndexError` class
    ///
    inline value::ClassT<value::Exception> const IndexError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eIndexError);
    /// `StopIteration` class
    ///
    inline value::ClassT<value::Exception> const StopIteration =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eStopIteration);
    /// `KeyError` class
    ///
    inline value::ClassT<value::Exception> const KeyError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eKeyError);
    /// `RangeError` class
    ///
    inline value::ClassT<value::Exception> const RangeError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eRangeError);
    /// `IOError` class
    ///
    inline value::ClassT<value::Exception> const IOError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eIOError);
    /// `RuntimeError` class
    ///
    inline value::ClassT<value::Exception> const RuntimeError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eRuntimeError);
    /// `FrozenError` class
    ///
    inline value::ClassT<value::Exception> const FrozenError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eFrozenError);
    /// `SecurityError` class
    ///
    inline value::ClassT<value::Exception> const SecurityError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eSecurityError);
    /// `SystemCallError` class
    ///
    inline value::ClassT<value::Exception> const SystemCallError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eSystemCallError);
    /// `ThreadError` class
    ///
    inline value::ClassT<value::Exception> const ThreadError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eThreadError);
    /// `TypeError` class
    ///
    inline value::ClassT<value::Exception> const TypeError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eTypeError);
    /// `ZeroDivisionError` class
    ///
    inline value::ClassT<value::Exception> const ZeroDivisionError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eZeroDivError);
    /// `NotImplementedError` class
    ///
    inline value::ClassT<value::Exception> const NotImplementedError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eNotImpError);
    /// `NoMemoryError` class
    ///
    inline value::ClassT<value::Exception> const NoMemoryError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eNoMemError);
    /// `NoMethodError` class
    ///
    inline value::ClassT<value::Exception> const NoMethodError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eNoMethodError);
    /// `FloatDomainError` class
    ///
    inline value::ClassT<value::Exception> const FloatDomainError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eFloatDomainError);
    /// `LocalJumpError` class
    ///
    inline value::ClassT<value::Exception> const LocalJumpError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eLocalJumpError);
    /// `SystemStackError` class
    ///
    inline value::ClassT<value::Exception> const SystemStackError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eSysStackError);
    /// `RegexpError` class
    ///
    inline value::ClassT<value::Exception> const RegexpError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eRegexpError);
    /// `EncodingError` class
    ///
    inline value::ClassT<value::Exception> const EncodingError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eEncodingError);
    /// `Encoding::CompatibilityError` class
    ///
    inline value::ClassT<value::Exception> const EncodingCompatibilityError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eEncCompatError);
    /// `NoMatchingPatternError` class
    ///
    inline value::ClassT<value::Exception> const NoMatchingPatternError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eNoMatchingPatternError);
    /// `NoMatchingPatternKeyError` class
    ///
    inline value::ClassT<value::Exception> const NoMatchingPatternKeyError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eNoMatchingPatternKeyError);
    /// `ScriptError` class
    ///
    inline value::ClassT<value::Exception> const ScriptError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eScriptError);
    /// `NameError` class
    ///
    inline value::ClassT<value::Exception> const NameError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eNameError);
    /// `SyntaxError` class
    ///
    inline value::ClassT<value::Exception> const SyntaxError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eSyntaxError);
    /// `LoadError` class
    ///
    inline value::ClassT<value::Exception> const LoadError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eLoadError);
    /// `Math::DomainError` class
    ///
    inline value::ClassT<value::Exception> const MathDomainError =
        detail::unsafe_coerce<value::ClassT<value::Exception>>(::rb_eMathDomainError);

#ifdef RCX_IO_BUFFER
    /// `IO::Buffer` class
    ///
    inline value::Class const IOBuffer = detail::unsafe_coerce<value::Class>(::rb_cIOBuffer);
#endif
  };

  namespace typed_data {
    template <typename> class DataTypeStorage;
  }

  /// Garbage collection.
  namespace gc {
    /// Phases of garbage collection.
    ///
    enum Phase {
      Marking,
      Compaction,
    };

    struct Gc {
      /// Marks an object as movable.
      ///
      /// The object will be marked as movable. In the compaction phase, the object may be moved.
      ///
      /// @tparam T The type of the object to be marked.
      /// @param value The object to be marked.
      template <std::derived_from<ValueBase> T> void mark_movable(T &value) const noexcept;
      /// Marks an object as pinned.
      ///
      /// The object will be marked as pinned. The object will not be moved while this reference
      /// is alive.
      ///
      /// @param value The object to be marked.
      void mark_pinned(ValueBase value) const noexcept;

    private:
      Phase phase_;

      Gc(Phase phase);
      template <typename> friend class typed_data::DataTypeStorage;
    };
  }

  namespace typed_data {
    template <typename T> void dmark(gc::Gc, T *RCX_Nonnull) noexcept;
    template <typename T> void dfree(T *RCX_Nonnull) noexcept;
    template <typename T> size_t dsize(T const *RCX_Nonnull) noexcept;

    struct AssociatedValue {
      std::optional<Value> value_;

    public:
      AssociatedValue() = default;
      AssociatedValue(AssociatedValue const &) noexcept: value_{} {
      }
      AssociatedValue &operator=(AssociatedValue const &) noexcept {
        return *this;
      }

      void associate_value(Value v) {
        if(value_) {
          throw std::runtime_error("Already associcated");
        }
        value_ = v;
      }

      std::optional<Value> get_associated_value() const {
        return value_;
      };

      void mark_associated_value(gc::Gc gc) noexcept {
        if(value_) {
          gc.mark_movable(*value_);
        }
      }
    };

    struct OneWayAssociation {};
    struct TwoWayAssociation: public AssociatedValue {};

    template <std::derived_from<TwoWayAssociation> T>
    void dmark(gc::Gc gc, T *RCX_Nonnull p) noexcept;

    struct WrappedStructBase {};

    template <typename AssociationPolicy = OneWayAssociation>
    struct WrappedStruct: public WrappedStructBase, public AssociationPolicy {};

    template <typename T, typename S>
    ClassT<T> bind_data_type(ClassT<T> klass, ClassT<S> superclass);

    template <typename T> class DataTypeStorage {
      inline static std::optional<rb_data_type_t> data_type_;

    public:
      static void bind(ClassT<T> klass, rb_data_type_t const *RCX_Nullable parent = nullptr);
      static ClassT<T> bound_class();
      static rb_data_type_t const *RCX_Nonnull get();

      template <typename... A>
        requires std::constructible_from<T, A...>
      static Value initialize(Value value, A &&...args);

      static Value initialize_copy(Value value, T const &obj)
        requires std::copy_constructible<T>;

      // TODO: allocator support
    };

    template <typename T> using DataType = DataTypeStorage<std::remove_cvref_t<T>>;
  }

  namespace convert {
    template <std::derived_from<typed_data::WrappedStructBase> T> struct FromValue<T> {
      std::reference_wrapper<T> convert(Value value);
    };
    template <std::derived_from<typed_data::TwoWayAssociation> T> struct IntoValue<T> {
      Value convert(T &value);
    };
    template <std::derived_from<typed_data::WrappedStructBase> T> struct FromValue<ClassT<T>> {
      ClassT<T> convert(Value value);
    };
  }

  /// Leaking object container.
  ///
  /// The contained Ruby object will not be garbage-collected or moved.
  /// Use this container if you want to store Ruby objects in global variables or static block
  /// variables.
  template <std::derived_from<ValueBase> T> class Leak {
    union {
      // T has a VALUE as its first field.
      T value_;
      VALUE raw_value_;
    };
    bool init_;

  public:
    /// Initializes the container with no value.
    ///
    Leak() noexcept;
    Leak(Leak<T> const &) = rcx_delete("Leak<T> cannot be copied");
    /// Initializes the container with the given value.
    ///
    Leak(T value) noexcept(noexcept(T(value)));
    Leak<T> &operator=(Leak<T> const &) = rcx_delete("Leak<T> cannot be copied");
    /// Copies the given value into the container, destroying the existing value if any.
    ///
    Leak<T> &operator=(T value) noexcept(noexcept(T(value)));
    /// Gets the value in the container.
    ///
    /// @throw std::runtime_error When the container has no value.
    T get() const;
    /// Copies the given value into the container, destroying the existing value if any.
    ///
    void set(T value) noexcept(noexcept(T(value)));
    /// Gets the value in the container.
    ///
    /// @throw std::runtime_error When the container has no value.
    T operator*() const;
    T const *RCX_Nonnull operator->() const;
    /// Clears the container.
    ///
    /// The value originally in the container will be no longer pinned.
    void clear() noexcept;
  };
  template <std::derived_from<ValueBase> T> Leak(T) -> Leak<T>;

  class Ruby {
  public:
    Module define_module(concepts::Identifier auto &&name);

    template <typename T = Value, typename S>
    ClassT<T> define_class(concepts::Identifier auto &&name, ClassT<S> superclass);

    template <typename T = Value> ClassT<T> define_class(concepts::Identifier auto &&name);
  };

  namespace detail {
    inline Ruby &unsafe_ruby() {
      static Ruby ruby = {};
      return ruby;
    }

    inline Ruby &ruby() {
      // TODO: check gvl
      return unsafe_ruby();
    }
  }
}

namespace std {
  template <std::derived_from<rcx::Value> T> struct formatter<T, char> {
    bool inspect = false;

    template <typename ParseContext> constexpr ParseContext::iterator parse(ParseContext &ctx);
    template <typename FormatContext>
    FormatContext::iterator format(T value, FormatContext &ctx) const;
  };
}
