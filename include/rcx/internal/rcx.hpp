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

#define rcx_assert(expr) assert((expr))
#define rcx_delete(reason) delete

#ifdef HAVE_FEATURE_NULLABILITY
#define RCX_Nullable _Nullable
#define RCX_Nonnull _Nonnull
#else
#define RCX_Nullable
#define RCX_Nonnull
#endif

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
    class String;
    class Array;
  }
  using namespace value;

  namespace concepts {
    template <typename T>
    concept StringLike = requires {
      typename std::remove_cvref_t<T>::value_type;
      typename std::remove_cvref_t<T>::traits_type;
      typename std::basic_string_view<typename std::remove_cvref_t<T>::value_type,
          typename std::remove_cvref_t<T>::traits_type>;
    };

    template <typename T>
    concept Identifier = requires(T id) {
      { id.as_ID() } noexcept -> std::same_as<ID>;
    } || std::is_nothrow_constructible_v<Symbol, T>;
  }

  namespace detail {
    template <typename T> struct unsafe_coerce {
      VALUE value;

      unsafe_coerce(VALUE value): value{value} {
      }

      template <std::derived_from<T> U> unsafe_coerce(unsafe_coerce<U> other): value{other.value} {
      }
    };

    template <typename T> inline constexpr bool always_false_v = false;

    // Duplicated definition for cxstring and u8cxstring, because clang-18 does not
    // support template parameter deduction for type aliases.
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

  namespace convert {
    template <typename T> Value into_Value(T value);
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

    template <> struct FromValue<String> {
      String convert(Value value);
    };
    template <> struct FromValue<std::string_view> {
      std::string_view convert(Value value);
    };

    template <> struct FromValue<Array> {
      Array convert(Value value);
    };
  }
  using namespace convert;

  namespace concepts {
    template <typename T>
    concept ConvertibleFromValue = requires(Value v) { from_Value<T>(v); };

    template <typename T>
    concept ConvertibleIntoValue = requires(T v) {
      { into_Value<T>(v) } -> std::same_as<Value>;
    };
  }

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

    template <concepts::ConvertibleFromValue T = Value> constexpr inline Self<T> self;
    template <concepts::ConvertibleFromValue T = Value, detail::cxstring name = "">
    constexpr inline Arg<T, name> arg;
    template <concepts::ConvertibleFromValue T = Value, detail::cxstring name = "">
    constexpr inline ArgOpt<T, name> arg_opt;
    template <concepts::ConvertibleFromValue T = Value> constexpr inline ArgSplat<T> arg_splat;
  }

  namespace concepts {
    template <typename T>
    concept ArgSpec = requires(Ruby &ruby, Value self, std::span<Value> &args) {
      typename T::ResultType;
      { T::parse(ruby, self, args) } -> std::same_as<typename T::ResultType>;
    };
  }

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
  };

  namespace literals {
    template <detail::cxstring> String operator""_str();
    template <detail::u8cxstring> String operator""_str();
    template <detail::cxstring> String operator""_fstr();
    template <detail::u8cxstring> String operator""_fstr();
    template <detail::cxstring> Symbol operator""_sym();
    template <detail::u8cxstring> Symbol operator""_sym();
    template <detail::cxstring> Id operator""_id();
    template <detail::u8cxstring> Id operator""_id();
  }

  /**
   * Wrapper for static IDs.
   *
   * Static IDs are never garbage-collected and safe to store in the heap.
   */
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
    enum Nilability : bool {
      Nonnil = false,
      Nilable = true,
    };

    class ValueBase {
      VALUE value_;

    protected:
      constexpr ValueBase(VALUE value);

    public:
      constexpr ValueBase();
      constexpr VALUE as_VALUE() const;
      ValueBase(detail::unsafe_coerce<ValueBase> coerce): value_(coerce.value) {
      }

      bool is_nil() const;
      bool is_frozen() const;
      template <typename T> bool is_instance_of(ClassT<T> klass) const;
      template <typename T> bool is_kind_of(ClassT<T> klass) const;
    };

    class Nonnil {
      Nonnil() = rcx_delete("This type of Value is non-nilable.");
      Nonnil(Nonnil const &) = default;
      Nonnil &operator=(Nonnil const &) = default;
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

      String inspect() const;

      static Value const qnil;
      static Value const qtrue;
      static Value const qfalse;
      static Value const qundef;
    };

    class Module: public ValueT<Module, Value> {
    public:
      using ValueT<Module, Value>::ValueT;

      /**
       * Returns the name path of this module.
       */
      String name() const;

      /**
       * Defines a module under this module.
       *
       * @warning Modules defined this way will be never garbage-collected.
       *
       * @param name Name of the module.
       * @return The newly created module.
       */
      Module define_module(concepts::Identifier auto &&name) const;

      /**
       * Defines a class under this module.
       *
       * @warning Classes defined this way will be never garbage-collected.
       *
       * @param name Name of the class.
       * @param superclass The new class will be a subclass of this class.
       * @return The newly created class.
       */
      template <typename T = Value, typename S>
      ClassT<T> define_class(concepts::Identifier auto &&name, ClassT<S> superclass) const;

      /**
       * Defines a subclass of Object under this module.
       *
       * @warning Classes defined this way will be never garbage-collected.
       *
       * @param name Name of the class.
       * @return The newly created class.
       */
      template <typename T = Value> ClassT<T> define_class(concepts::Identifier auto &&name) const;

      /**
       * Defines an instance method.
       *
       * @warning Defining method this way allocates a resource that is never freeable.
       *
       * @tparam Self The type of self.
       * @param mid The name of the method.
       * @param argspec List of argument specificatios.
       * @return Self.
       */
      template <concepts::ConvertibleFromValue Self = Value, concepts::ArgSpec... ArgSpec>
      Module define_method(concepts::Identifier auto &&mid,
          std::invocable<Self, typename ArgSpec::ResultType...> auto &&function,
          ArgSpec... argspec) const;

      /**
       * Checks if a constant is defined under this module.
       *
       * @param name Name of the constant.
       * @returns Whether the constant is defined.
       */
      bool const_defined(concepts::Identifier auto &&name) const;

      /**
       * Gets the value of a constant under this module.
       *
       * @tparam T The type the constant value should be converted into.
       * @param name Name of the constant.
       * @return The value covnerted into T.
       */
      template <concepts::ConvertibleFromValue T = Value>
      T const_get(concepts::Identifier auto &&name) const;

      /**
       * Defines a constant with a value under this module.
       *
       * @param name The name of the constant.
       * @param value The value to be set.
       */
      void const_set(
          concepts::Identifier auto &&name, concepts::ConvertibleIntoValue auto &&value) const;

      /**
       * Creates an anonymous module.
       *
       * @return The newly created module.
       */
      static Module new_module();
    };

    template <typename T>
    class [[clang::preferred_name(Class)]] ClassT: public ValueT<ClassT<T>, Module> {
    public:
      using ValueT<ClassT<T>, Module>::ValueT;

      auto new_instance(concepts::ConvertibleIntoValue auto &&...args) const -> auto;

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
    };

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

  namespace gc {
    struct Marking;
    struct Compaction;
  }

  namespace typed_data {
    template <typename T> void dmark(gc::Marking, T *RCX_Nonnull) noexcept;
    template <typename T> void dfree(T *RCX_Nonnull) noexcept;
    template <typename T> size_t dsize(T const *RCX_Nonnull) noexcept;
    template <typename T> void dcompact(gc::Compaction, T *RCX_Nonnull) noexcept;

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

      void replace_associated_value(std::invocable<Value> auto f) noexcept {
        if(value_) {
          value_ = f(*value_);
        }
      }
    };

    struct OneWayAssociation {};
    struct TwoWayAssociation: public AssociatedValue {};

    template <std::derived_from<TwoWayAssociation> T>
    void dmark(gc::Marking gc, T *RCX_Nonnull p) noexcept;
    template <std::derived_from<TwoWayAssociation> T>
    void dcompact(gc::Compaction gc, T *RCX_Nonnull p) noexcept;

    struct WrappedStructBase {};

    template <typename AssociationPolicy = OneWayAssociation>
    struct WrappedStruct: public WrappedStructBase, public AssociationPolicy {};

    template <typename T, typename S>
    ClassT<T> bind_data_type(ClassT<T> klass, ClassT<S> superclass);

    template <typename T> class DataTypeStorage {
      inline static std::optional<rb_data_type_t> data_type_;

    public:
      static void bind(ClassT<T> klass, rb_data_type_t const *RCX_Nullable parent = nullptr);
      static rb_data_type_t const *RCX_Nonnull get();
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

  namespace gc {
    struct Marking {
      void mark_movable(Value value) const noexcept;
      void mark_pinned(Value value) const noexcept;

    private:
      Marking() = default;
      template <typename> friend class typed_data::DataTypeStorage;
    };

    struct Compaction {
      template <std::derived_from<ValueBase> T> T new_location(T value) const noexcept;

    private:
      Compaction() = default;
      template <typename> friend class typed_data::DataTypeStorage;
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
