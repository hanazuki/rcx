// SPDX-License-Identifier: BSL-1.0
// SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>
#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
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
  /// @private
  namespace detail {
    template <typename T> struct unsafe_coerce {
      VALUE value;

      unsafe_coerce(VALUE value): value{value} {
      }

      template <std::derived_from<T> U> unsafe_coerce(unsafe_coerce<U> other): value{other.value} {
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

    template <typename F> auto protect(F functor) -> auto;
    template <typename A, typename R>
      requires(std::is_integral_v<A> && sizeof(A) == sizeof(VALUE) && std::is_integral_v<R> &&
               sizeof(R) == sizeof(VALUE))
    VALUE protect(VALUE (*RCX_Nonnull func)(A), A arg);

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

    template <> struct FromValue<Module> {
      Module convert(Value value);
    };

    template <typename T> struct FromValue<ClassT<T>> {
      ClassT<T> convert(Value value);
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
    template <> struct FromValue<std::string_view> {
      std::string_view convert(Value value);
    };

    template <> struct FromValue<Array> {
      Array convert(Value value);
    };

#ifdef RCX_IO_BUFFER
    template <> struct FromValue<IOBuffer> {
      IOBuffer convert(Value value);
    };
#endif
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

    template <concepts::ConvertibleFromValue T = Value> struct ArgSplat {
      using ResultType = std::vector<detail::wrap_ref_t<T>>;
      static ResultType parse(Ruby &, Value self, std::span<Value> &args);
    };

    struct Block {
      using ResultType = Proc;
      static ResultType parse(Ruby &, Value self, std::span<Value> &args);
    };

    template <concepts::ConvertibleFromValue T = Value> constexpr inline Self<T> self;
    template <concepts::ConvertibleFromValue T = Value, detail::cxstring name = "">
    constexpr inline Arg<T, name> arg;
    template <concepts::ConvertibleFromValue T = Value, detail::cxstring name = "">
    constexpr inline ArgOpt<T, name> arg_opt;
    template <concepts::ConvertibleFromValue T = Value> constexpr inline ArgSplat<T> arg_splat;
    constexpr inline Block block;
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

  /**
   * Built-in classes.
   */
  namespace builtin {
    value::Class NilClass();
    value::Class TrueClass();
    value::Class FalseClass();
    ClassT<value::Class> Class();
    ClassT<value::Module> Module();
    value::Class Object();
    ClassT<value::String> String();
    ClassT<value::Array> Array();
    value::Class RuntimeError();
    value::Class RangeError();
    value::Class ArgumentError();
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
    /// Creates an `Id` for the name encoded in ASCII/ASCII-8BIT.
    ///
    template <detail::cxstring> Id operator""_id();
    /// Creates an `Id` for the name encoded in UTF-8.
    ///
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
      ValueT(detail::unsafe_coerce<Derived> coerce): Super(coerce) {
      }
      Derived &operator=(Derived const &other) {
        Super::operator=(other);
        return *this;
      }

      ClassT<Derived> get_class() const;
      Derived freeze() const;

      template <concepts::ConvertibleFromValue Self = Value, concepts::ArgSpec... ArgSpec>
      Derived define_singleton_method(concepts::Identifier auto &&mid,
          std::invocable<Self, typename ArgSpec::ResultType...> auto &&function,
          ArgSpec... argspec) const;
    };

    class Value: public ValueT<Value, ValueBase, Nilable> {
    public:
      using ValueT<Value, ValueBase, Nilable>::ValueT;

      template <concepts::ConvertibleFromValue R = Value>
      R send(concepts::Identifier auto &&mid, concepts::ConvertibleIntoValue auto &&...args) const;

      bool test() const;
      String inspect() const;

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
      /// @returns Uninitialized object.
      Value allocate() const;

      template <concepts::ArgSpec... ArgSpec>
      ClassT<T> define_method(concepts::Identifier auto &&mid,
          std::invocable<T &, typename ArgSpec::ResultType...> auto &&function,
          ArgSpec... argspec) const;

      template <concepts::ArgSpec... ArgSpec>
      ClassT<T> define_method_const(concepts::Identifier auto &&mid,
          std::invocable<T const &, typename ArgSpec::ResultType...> auto &&function,
          ArgSpec... argspec) const;

      template <concepts::ArgSpec... ArgSpec>
        requires std::constructible_from<T, typename ArgSpec::ResultType...>
      ClassT<T> define_constructor(ArgSpec... argspec) const;

      ClassT<T> define_copy_constructor() const
        requires std::copy_constructible<T>;

      static Class new_class();
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

      String locktmp() const;
      String unlocktmp() const;
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

#ifdef RCX_IO_BUFFER
    /// Represents an `IO::Buffer` object.
    class IOBuffer: public ValueT<IOBuffer, Value> {
    public:
      using ValueT<IOBuffer, Value>::ValueT;

      /// Creates an `IO::Buffer` with internal storage.
      static IOBuffer new_internal(size_t size);
      /// Creates an `IO::Buffer` with mapped storage.
      static IOBuffer new_mapped(size_t size);
      /// Creates an `IO::Buffer` with externally managed storage. The returned `IO::Buffer` should
      /// be `free`d when the underlying storage is longer valid.
      template <size_t N = std::dynamic_extent>
      static IOBuffer new_external(std::span<std::byte, N> bytes [[clang::lifetimebound]]);
      /// Creates an `IO::Buffer` with externally managed read-only storage. The returned
      /// `IO::Buffer` should be `free`d when the underlying storage is longer valid.
      template <size_t N = std::dynamic_extent>
      static IOBuffer new_external(std::span<std::byte const, N> bytes [[clang::lifetimebound]]);

      /// Frees the internal storage or disassociates the external storage (See the Ruby
      /// documentation for `IO::Buffer#free`).
      void free() const;
      /// Resizes the `IO::Buffer` to the given size.
      void resize(size_t size) const;

      /// Returns the bytes of the `IO::Buffer`. This will raise if the `IO::Buffer` is not
      /// writable.
      std::span<std::byte> bytes() const;
      /// Returns the bytes of the `IO::Buffer` as a read-only span.
      std::span<std::byte const> cbytes() const;

      // BasicLockable
      /// Locks the `IO::Buffer`.
      void lock() const;
      /// Unlocks the `IO::Buffer`.
      void unlock() const;

      // Lockable
      /// Tries to lock the `IO::Buffer`.
      bool try_lock() const;
    };
#endif

    template <std::derived_from<ValueBase> T> class PinnedOpt {
    protected:
      struct Storage {
        // ValueBase has VALUE as its first field.
        T value;

        Storage(T v);
        ~Storage();
        Storage &operator=(Storage const &) = delete;
      };

      std::shared_ptr<Storage> ptr_;

    public:
      PinnedOpt() noexcept = default;
      explicit PinnedOpt(T value);
      T &operator*() const noexcept;
      T *RCX_Nullable operator->() const noexcept;
      operator bool() const noexcept;
    };

    template <std::derived_from<ValueBase> T> class Pinned: public PinnedOpt<T> {
    public:
      Pinned() = rcx_delete("Pinned<T> must always have a value of T; "
                            "use PinnedOpt<T> instread to allow missing value.");
      explicit Pinned(T value);
      T *RCX_Nonnull operator->() const noexcept;
    };

    template <std::derived_from<ValueBase> T> PinnedOpt(T) -> PinnedOpt<T>;
    template <std::derived_from<ValueBase> T> Pinned(T) -> Pinned<T>;
  }

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
      /// The object will be marked as pinned. The object will not be moved while this reference is
      /// alive.
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
  }

  class Ruby {
  public:
    Module define_module(concepts::Identifier auto &&name);

    template <typename T = Value, typename S>
    ClassT<T> define_class(concepts::Identifier auto &&name, ClassT<S> superclass);

    template <typename T = Value> ClassT<T> define_class(concepts::Identifier auto &&name);
  };

  class RubyError {
    Value exception_;

  public:
    explicit RubyError(Value exception) noexcept;

    Value exception() const noexcept;

    template <typename... Args>
    static RubyError format(Class cls, std::format_string<Args...> fmt, Args &&...args);
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
