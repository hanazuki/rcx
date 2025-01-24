// SPDX-License-Identifier: BSL-1.0
// SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>

#include <concepts>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include <ffi.h>
#include <rcx/internal/rcx.hpp>

#if HAVE_CXXABI_H
#include <cxxabi.h>
#endif

namespace rcx {
  namespace detail {
#if HAVE_ABI___CXA_DEMANGLE
    static bool constexpr have_abi_cxa_demangle = true;
#else
    static bool constexpr have_abi_cxa_demangle = false;
#endif
#if HAVE_ABI___CXA_CURRENT_EXCEPTION_TYPE
    static bool constexpr have_abi_cxa_current_exception_type = true;
#else
    static bool constexpr have_abi_cxa_current_exception_type = false;
#endif

    auto cxx_protect(std::invocable<> auto const &functor) noexcept
        -> std::invoke_result_t<decltype(functor)>;

    inline NativeRbFunc *RCX_Nonnull alloc_callback(std::function<RbFunc> f) {
      static std::array argtypes = {
        &ffi_type_sint,     // int argc
        &ffi_type_pointer,  // VALUE *argv
        &ffi_type_pointer,  // VALUE self (has the same size as void *)
      };
      static ffi_cif cif = [] {
        ffi_cif cif;
        if(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, argtypes.size(), &ffi_type_pointer,
               argtypes.data()) != FFI_OK) {
          throw std::runtime_error("ffi_prep_cif failed");
        }
        return cif;
      }();
      static auto const trampoline = [](ffi_cif *RCX_Nonnull, void *RCX_Nonnull ret,
                                         void *RCX_Nonnull args[], void *RCX_Nonnull function) {
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        auto argc = *reinterpret_cast<int *>(args[0]);
        auto argv = *reinterpret_cast<Value **>(args[1]);
        auto self = *reinterpret_cast<Value *>(args[2]);
        // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        *reinterpret_cast<Value *>(ret) = cxx_protect([&] {
          return (*reinterpret_cast<decltype(f) *>(function))(std::span<Value>(argv, argc), self);
        });
      };

      void *callback = nullptr;
      auto closure = static_cast<ffi_closure *>(ffi_closure_alloc(sizeof(ffi_closure), &callback));

      if(!closure) {
        throw std::runtime_error{"ffi_closure_alloc failed"};
      }

      if(ffi_prep_closure_loc(closure, &cif, trampoline,
             new decltype(f)(std::move(f)),  // let it leak
             callback) != FFI_OK) {
        throw std::runtime_error{"ffi_prep_closure_loc failed"};
      }

      return reinterpret_cast<NativeRbFunc *>(callback);
    }

    template <concepts::ArgSpec... ArgSpec> struct Parser {
      std::span<Value> args;
      Value self;

      auto parse(Ruby &ruby, std::invocable<typename ArgSpec::ResultType...> auto &&func)
          -> std::invoke_result_t<decltype(func), typename ArgSpec::ResultType...> {
        // Experssions in an initializer list is evaluated from left to right, in contrast to
        // function arguments.
        return std::apply(std::forward<decltype(func)>(func),
            std::tuple<typename ArgSpec::ResultType...>{ArgSpec::parse(ruby, self, args)...});
      }
    };

    template <typename... ArgSpec> struct method_callback {
      template <typename F> static NativeRbFunc *RCX_Nonnull alloc(F &&function) {
        return alloc_callback([function](std::span<Value> args, Value self) -> Value {
          Parser<ArgSpec...> parser{args, self};
          using Result = decltype(parser.parse(detail::unsafe_ruby(), std::move(function)));
          if constexpr(std::is_void_v<Result>) {
            parser.parse(detail::unsafe_ruby(), std::move(function));
            return {};
          } else {
            return into_Value<Result>(parser.parse(detail::unsafe_ruby(), function));
          }
        });
      }
    };

    inline void check_jump_tag(int state) {
      enum {
        RUBY_TAG_NONE = 0,
        RUBY_TAG_RAISE = 6,
      };

      switch(state) {
      case RUBY_TAG_NONE:
        return;
      case RUBY_TAG_RAISE: {
        auto err = detail::unsafe_coerce<Value>(rb_errinfo());
        rb_set_errinfo(RUBY_Qnil);
        throw RubyError(err);
      }
      default:
        throw Jump(state);
      }
    }

    template <typename F> inline auto protect(F functor) -> auto {
      int state = 0;

      using Result = std::invoke_result_t<F>;
      if constexpr(std::is_void_v<Result>) {
        ::rb_protect(
            [](VALUE callback) {
              (*reinterpret_cast<F *>(callback))();
              return RUBY_Qnil;
            },
            reinterpret_cast<VALUE>(&functor), &state);
        check_jump_tag(state);
        return;
      } else {
        std::optional<Result> result;
        auto callback = [&functor, &result]() { *result = functor(); };
        using Callback = decltype(callback);

        ::rb_protect(
            [](VALUE callback) {
              (*reinterpret_cast<Callback *>(callback))();
              return RUBY_Qnil;
            },
            reinterpret_cast<VALUE>(&callback), &state);
        check_jump_tag(state);
        return std::move(*result);
      }
    }

    template <typename A, typename R>
      requires(std::is_integral_v<A> && sizeof(A) == sizeof(VALUE) && std::is_integral_v<R> &&
               sizeof(R) == sizeof(VALUE))
    inline R protect(R (*RCX_Nonnull func)(A), A arg) {
      int state = 0;

      auto result =
          reinterpret_cast<R>(::rb_protect(reinterpret_cast<VALUE (*RCX_Nonnull)(VALUE)>(func),
              reinterpret_cast<VALUE>(arg), &state));
      check_jump_tag(state);
      return result;
    }

    /**
     * Assumes the (C) function doesn't throw C++ exceptions.
     */
    template <typename R, typename... A>
    auto assume_noexcept(R (*RCX_Nonnull f)(A...)) noexcept -> R (*RCX_Nonnull)(A...) noexcept {
      return reinterpret_cast<R (*)(A...) noexcept>(f);
    }
    template <typename R, typename... A>
    auto assume_noexcept(R (*RCX_Nonnull f)(A..., ...)) noexcept
        -> R (*RCX_Nonnull)(A..., ...) noexcept {
      return reinterpret_cast<R (*)(A..., ...) noexcept>(f);
    }

    /**
     * Converts anything into ID.
     *
     * Do not store the return value. Dynamic IDs may be garbage-collected.
     */
    template <concepts::Identifier I> inline decltype(auto) into_ID(I &&id) noexcept {
      if constexpr(requires {
                     { id.as_ID() } noexcept -> std::same_as<ID>;
                   }) {
        return id.as_ID();
      } else {
        return Symbol(std::forward<decltype(id)>(id)).as_ID();
      }
    }

    // Calculate the default type of the self parameter for the methods of ClassT<T>.
    template <concepts::ConvertibleFromValue T>
    using self_type =
        std::conditional_t<std::derived_from<T, typed_data::WrappedStructBase>, T &, T>;
    template <concepts::ConvertibleFromValue T>
    using self_type_const =
        std::conditional_t<std::derived_from<T, typed_data::WrappedStructBase>, T const &, T const>;
  };

  namespace arg {
    template <concepts::ConvertibleFromValue T>
    inline typename Self<T>::ResultType Self<T>::parse(Ruby &, Value self, std::span<Value> &) {
      return from_Value<T>(self);
    }

    template <concepts::ConvertibleFromValue T, detail::cxstring name>
    inline typename Arg<T, name>::ResultType Arg<T, name>::parse(
        Ruby &, Value, std::span<Value> &args) {
      if(args.empty()) {
        rb_raise(rb_eArgError, "Missing required argument (%s)", name.data());
      }
      auto arg = from_Value<T>(args.front());
      args = std::ranges::drop_view(args, 1);
      return arg;
    }

    template <concepts::ConvertibleFromValue T, detail::cxstring name>
    inline typename ArgOpt<T, name>::ResultType ArgOpt<T, name>::parse(
        Ruby &, Value, std::span<Value> &args) {
      if(args.empty()) {
        return std::nullopt;
      }
      auto arg = from_Value<T>(args.front());
      args = std::ranges::drop_view(args, 1);
      return arg;
    }

    template <concepts::ConvertibleFromValue T>
    inline typename ArgSplat<T>::ResultType ArgSplat<T>::parse(
        Ruby &, Value, std::span<Value> &args) {
      typename ArgSplat<T>::ResultType values;
      values.reserve(args.size());
      while(!args.empty()) {
        values.push_back(from_Value<T>(args.front()));
        args = std::ranges::drop_view(args, 1);
      }
      return args;
    }

    inline Block::ResultType Block::parse(Ruby &, Value, std::span<Value> &) {
      return detail::unsafe_coerce<Proc>(detail::protect([] { return ::rb_block_proc(); }));
    }
  }

  namespace convert {
    template <typename T> inline Value into_Value(T value) {
      if constexpr(std::convertible_to<T, Value>) {
        return value;
      } else {
        return IntoValue<std::remove_reference_t<T>>().convert(value);
      }
    }
    template <typename T> inline auto from_Value(Value value) -> auto {
      if constexpr(std::convertible_to<Value, T>) {
        return value;
      } else {
        return FromValue<std::remove_reference_t<T>>().convert(value);
      }
    }

    inline bool FromValue<bool>::convert(Value value) {
      return RB_TEST(value.as_VALUE());
    }
    inline Value IntoValue<bool>::convert(bool value) {
      return detail::unsafe_coerce<Value>(value ? RUBY_Qtrue : RUBY_Qfalse);
    };

    inline signed char FromValue<signed char>::convert(Value value) {
      int const v = NUM2INT(value.as_VALUE());
      signed char const r = static_cast<signed char>(v);
      if(v != static_cast<int>(r)) {
        throw RubyError::format(builtin::RangeError,
            "integer {} too {} to convert to 'signed char'", v, v < 0 ? "small" : "big");
      }
      return r;
    }
    inline Value IntoValue<signed char>::convert(signed char value) {
      return detail::unsafe_coerce<Value>(INT2FIX(value));
    };

    inline unsigned char FromValue<unsigned char>::convert(Value value) {
      int const v = NUM2INT(value.as_VALUE());
      unsigned char const r = static_cast<unsigned char>(v);
      if(v != static_cast<int>(r)) {
        throw RubyError::format(builtin::RangeError,
            "integer {} too {} to convert to 'unsigned char'", v, v < 0 ? "small" : "big");
      }
      return r;
    }
    inline Value IntoValue<unsigned char>::convert(unsigned char value) {
      return detail::unsafe_coerce<Value>(INT2FIX(value));
    };

#define RCX_DEFINE_CONV(TYPE, FROM_VALUE, INTO_VALUE)                                              \
  inline TYPE FromValue<TYPE>::convert(Value value) {                                              \
    return detail::protect([v = value.as_VALUE()] { return FROM_VALUE(v); });                      \
  }                                                                                                \
  inline Value IntoValue<TYPE>::convert(TYPE value) {                                              \
    return detail::unsafe_coerce<Value>(INTO_VALUE(value));                                        \
  }

    RCX_DEFINE_CONV(short, RB_NUM2SHORT, RB_INT2FIX);
    RCX_DEFINE_CONV(unsigned short, RB_NUM2USHORT, RB_INT2FIX);
    RCX_DEFINE_CONV(int, RB_NUM2INT, RB_INT2NUM);
    RCX_DEFINE_CONV(unsigned int, RB_NUM2UINT, RB_UINT2NUM);
    RCX_DEFINE_CONV(long, RB_NUM2LONG, RB_LONG2NUM);
    RCX_DEFINE_CONV(unsigned long, RB_NUM2ULONG, RB_ULONG2NUM);
    RCX_DEFINE_CONV(long long, RB_NUM2LL, RB_LL2NUM);
    RCX_DEFINE_CONV(unsigned long long, RB_NUM2ULL, RB_ULL2NUM);
    RCX_DEFINE_CONV(double, rb_num2dbl, rb_float_new);

#undef RCX_DEFINE_CONV

    template <std::derived_from<typed_data::WrappedStructBase> T>
    inline std::reference_wrapper<T> FromValue<T>::convert(Value value) {
      if constexpr(!std::is_const_v<T>) {
        detail::protect([&] { ::rb_check_frozen(value.as_VALUE()); });
      }
      auto const data = rb_check_typeddata(value.as_VALUE(), typed_data::DataType<T>::get());
      if(!data) {
        throw std::runtime_error{"Object is not yet initialized"};
      }
      return std::ref(*static_cast<T *>(data));
    }

    template <std::derived_from<typed_data::TwoWayAssociation> T>
    inline Value IntoValue<T>::convert(T &value) {
      if(auto const v = value.get_associated_value()) {
        return *v;
      }
      throw std::runtime_error{"This object is not managed by Ruby"};
    }
  }

  namespace typed_data {
    template <typename T> void dmark(gc::Gc, T *RCX_Nonnull) noexcept {
      // noop
    }
    template <typename T> void dfree(T *RCX_Nonnull p) noexcept {
      delete p;
    }
    template <typename T> size_t dsize(T const *RCX_Nonnull) noexcept {
      return sizeof(T);
    }

    template <typename T, typename S>
    inline ClassT<T> bind_data_type(ClassT<T> klass, ClassT<S> superclass) {
      if constexpr(std::derived_from<T, typed_data::WrappedStructBase>) {
        rb_data_type_t const *RCX_Nullable parent = nullptr;
        if constexpr(!std::derived_from<S, Value>) {
          parent = DataType<S>::get();

          if(reinterpret_cast<VALUE>(parent->data) != superclass.as_VALUE()) {
            throw std::runtime_error("superclass has mismatching static type");
          }
        }

        DataType<T>::bind(klass, parent);
      }
      return klass;
    }

    template <typename T> inline rb_data_type_t const *RCX_Nonnull DataTypeStorage<T>::get() {
      if(!data_type_)
        throw std::runtime_error(
            std::format("Type '{}' is not yet bound to a Ruby Class", typeid(T).name()));
      return &*data_type_;
    }

    template <typename T> inline ClassT<T> DataTypeStorage<T>::bound_class() {
      return detail::unsafe_coerce<ClassT<T>>(reinterpret_cast<VALUE>(get()->data));
    }

    template <typename T>
    inline void DataTypeStorage<T>::bind(
        ClassT<T> klass, rb_data_type_t const *RCX_Nullable parent) {
      if(data_type_) {
        throw std::runtime_error{"This type is already bound to a Ruby Class"};
      }

      data_type_ = rb_data_type_t{
        .wrap_struct_name = strdup(klass.name().data()),  // let it leek
        .function = {
          .dmark =
              [](void *RCX_Nonnull p) noexcept {
                using typed_data::dmark;
                dmark(gc::Gc(gc::Phase::Marking), static_cast<T *>(p));
              },
          .dfree =
              [](void *RCX_Nonnull p) noexcept {
                using typed_data::dfree;
                dfree(static_cast<T *>(p));
              },
          .dsize =
              [](void const *RCX_Nonnull p) noexcept {
                using typed_data::dsize;
                return dsize(static_cast<T const *>(p));
              },
          .dcompact =
              [](void *RCX_Nonnull p) noexcept {
                using typed_data::dmark;
                dmark(gc::Gc(gc::Phase::Compaction), static_cast<T *>(p));
              },
          // .reserved is zero-initialized
        },
        .parent = parent,
        .data = reinterpret_cast<void *>(klass.as_VALUE()),
        .flags = RUBY_TYPED_FREE_IMMEDIATELY,
      };
      ::rb_gc_register_address(reinterpret_cast<VALUE *>(&data_type_->data));

      ::rb_define_alloc_func(klass.as_VALUE(),
          [](VALUE klass) { return ::rb_data_typed_object_wrap(klass, nullptr, get()); });
    }

    template <typename T>
    template <typename... A>
      requires std::constructible_from<T, A...>
    inline Value DataTypeStorage<T>::initialize(Value value, A &&...args) {
      auto data = new T(std::forward<A>(args)...);
      RTYPEDDATA_DATA(value.as_VALUE()) = data;  // Tracked by Ruby GC
      if constexpr(std::derived_from<T, typed_data::TwoWayAssociation>) {
        data->associate_value(value);
      }
      return value;
    }

    template <typename T>
    inline Value DataTypeStorage<T>::initialize_copy(Value value, T const &obj)
      requires std::copy_constructible<T>
    {
      auto data = new T(obj);
      RTYPEDDATA_DATA(value.as_VALUE()) = data;  // Tracked by Ruby GC
      if constexpr(std::derived_from<T, typed_data::TwoWayAssociation>) {
        data->associate_value(value);
      }
      return value;
    }

    template <std::derived_from<TwoWayAssociation> T>
    inline void dmark(gc::Gc gc, T *RCX_Nonnull p) noexcept {
      p->mark_associated_value(gc);
    }
  }

  namespace gc {
    inline Gc::Gc(Phase phase): phase_(phase) {
    }

    template <std::derived_from<ValueBase> T>
    inline void Gc::mark_movable(T &value) const noexcept {
      switch(phase_) {
      case Phase::Marking:
        ::rb_gc_mark_movable(value.as_VALUE());
        break;
      case Phase::Compaction:
        value = detail::unsafe_coerce<T>(::rb_gc_location(value.as_VALUE()));
        break;
      default:;  // unreachable
      }
    }
    inline void Gc::mark_pinned(ValueBase value) const noexcept {
      switch(phase_) {
      case Phase::Marking:
        ::rb_gc_mark(value.as_VALUE());
        break;
      case Phase::Compaction:
        // no-op
        break;
      default:;  // unreachable
      }
    }
  }

  /// Id

  inline Id::Id(ID id): id_(id) {
  }

  inline ID Id::as_ID() const noexcept {
    return id_;
  }

  namespace value {
    /// ValueBase

    inline constexpr ValueBase::ValueBase(): value_(RUBY_Qnil) {
    }

    inline constexpr ValueBase::ValueBase(VALUE value): value_(value) {
    }

    inline constexpr VALUE ValueBase::as_VALUE() const {
      return value_;
    }

    inline bool ValueBase::is_nil() const {
      return RB_NIL_P(value_);
    }

    inline bool ValueBase::is_frozen() const {
      return ::rb_obj_frozen_p(as_VALUE());
    }

    template <typename T> inline bool ValueBase::is_instance_of(ClassT<T> klass) const {
      return detail::protect(
          [&] { return RBTEST(::rb_obj_is_instance_of(as_VALUE(), klass.as_VALUE())); });
    }

    template <typename T> inline bool ValueBase::is_kind_of(ClassT<T> klass) const {
      return detail::protect(
          [&] { return RBTEST(::rb_obj_is_kind_of(as_VALUE(), klass.as_VALUE())); });
    }

    /// ValueT

    template <typename Derived, std::derived_from<ValueBase> Super, Nilability nilable>
    ClassT<Derived> ValueT<Derived, Super, nilable>::get_class() const {
      return detail::unsafe_coerce<ClassT<Derived>>(
          detail::protect(::rb_obj_class, this->as_VALUE()));
    }

    template <typename Derived, std::derived_from<ValueBase> Super, Nilability nilable>
    Derived ValueT<Derived, Super, nilable>::freeze() const {
      return detail::unsafe_coerce<Derived>(detail::protect(::rb_obj_freeze, this->as_VALUE()));
    }

    template <typename Derived, std::derived_from<ValueBase> Super, Nilability nilable>
    template <concepts::ConvertibleFromValue Self, concepts::ArgSpec... ArgSpec>
    inline Derived ValueT<Derived, Super, nilable>::define_singleton_method(
        concepts::Identifier auto &&mid,
        std::invocable<Self, typename ArgSpec::ResultType...> auto &&function, ArgSpec...) const {
      auto const callback = detail::method_callback<arg::Self<Self>, ArgSpec...>::alloc(
          std::forward<decltype(function)>(function));
      detail::protect([&] {
        auto const singleton = ::rb_singleton_class(this->as_VALUE());
        rb_define_method_id(
            singleton, detail::into_ID(std::forward<decltype(mid)>(mid)), callback, -1);
      });
      return *static_cast<Derived const *>(this);
    }

    /// Value

    template <concepts::ConvertibleFromValue R>
    inline R Value::send(
        concepts::Identifier auto &&mid, concepts::ConvertibleIntoValue auto &&...args) const {
      return from_Value<R>(detail::unsafe_coerce<Value>(detail::protect([&] {
        return ::rb_funcall(as_VALUE(), detail::into_ID(std::forward<decltype(mid)>(mid)),
            sizeof...(args), into_Value(std::forward<decltype(args)>(args)).as_VALUE()...);
      })));
    }

    inline bool Value::test() const {
      return RB_TEST(as_VALUE());
    }

    inline String Value::inspect() const {
      return detail::unsafe_coerce<String>(
          detail::protect([&] { return ::rb_inspect(as_VALUE()); }));
    }

    inline String Value::to_string() const {
      return detail::unsafe_coerce<String>(
          detail::protect([&] { return ::rb_obj_as_string(as_VALUE()); }));
    }

    inline bool Value::instance_variable_defined(concepts::Identifier auto &&name) const {
      return detail::protect([&] {
        return ::rb_ivar_defined(as_VALUE(), detail::into_ID(std::forward<decltype(name)>(name)));
      });
    }

    template <concepts::ConvertibleFromValue T>
    inline auto Value::instance_variable_get(concepts::Identifier auto &&name) const -> auto {
      return from_Value<T>(detail::unsafe_coerce<Value>(detail::protect([&] {
        return ::rb_ivar_get(as_VALUE(), detail::into_ID(std::forward<decltype(name)>(name)));
      })));
    }

    inline void Value::instance_variable_set(
        concepts::Identifier auto &&name, concepts::ConvertibleIntoValue auto &&value) const {
      auto const v = into_Value(std::forward<decltype(value)>(value));
      return detail::protect([&] {
        ::rb_ivar_set(
            as_VALUE(), detail::into_ID(std::forward<decltype(name)>(name)), v.as_VALUE());
      });
    }

    inline Value const Value::qnil = {RUBY_Qnil};
    inline Value const Value::qtrue = {RUBY_Qtrue};
    inline Value const Value::qfalse = {RUBY_Qfalse};
    inline Value const Value::qundef = {RUBY_Qundef};
  }

  /// Module

  namespace value {
    inline Module Module::define_module(concepts::Identifier auto &&name) const {
      return detail::unsafe_coerce<Module>(::rb_define_module_id_under(
          as_VALUE(), detail::into_ID(std::forward<decltype(name)>(name))));
    }

    inline String Module::name() const {
      return detail::unsafe_coerce<String>(::rb_class_path(as_VALUE()));
    }

    template <typename T, typename S>
    inline ClassT<T> Module::define_class(
        concepts::Identifier auto &&name, ClassT<S> superclass) const {
      ClassT<T> klass = detail::unsafe_coerce<ClassT<T>>(detail::protect([&] {
        return ::rb_define_class_id_under(
            as_VALUE(), detail::into_ID(std::forward<decltype(name)>(name)), superclass.as_VALUE());
      }));
      return typed_data::bind_data_type(klass, superclass);
    }

    template <typename T>
    inline ClassT<T> Module::define_class(concepts::Identifier auto &&name) const {
      return define_class<T>(std::forward<decltype(name)>(name), builtin::Object);
    }

    template <concepts::ConvertibleFromValue Self, concepts::ArgSpec... ArgSpec>
    inline Module Module::define_method(concepts::Identifier auto &&mid,
        std::invocable<Self, typename ArgSpec::ResultType...> auto &&function, ArgSpec...) const {
      auto const callback = detail::method_callback<arg::Self<Self>, ArgSpec...>::alloc(function);
      detail::protect([&] {
        rb_define_method_id(
            as_VALUE(), detail::into_ID(std::forward<decltype(mid)>(mid)), callback, -1);
      });
      return *this;
    }

    inline Module Module::new_module() {
      return detail::unsafe_coerce<Module>(detail::protect([] { return ::rb_module_new(); }));
    }

    inline bool Module::const_defined(concepts::Identifier auto &&name) const {
      return detail::protect([&] {
        return ::rb_const_defined(as_VALUE(), detail::into_ID(std::forward<decltype(name)>(name)));
      });
    }

    template <concepts::ConvertibleFromValue T>
    inline T Module::const_get(concepts::Identifier auto &&name) const {
      return from_Value<T>(detail::unsafe_coerce<Value>(detail::protect([&] {
        return ::rb_const_get(as_VALUE(), detail::into_ID(std::forward<decltype(name)>(name)));
      })));
    }

    inline void Module::const_set(
        concepts::Identifier auto &&name, concepts::ConvertibleIntoValue auto &&value) const {
      auto const v = into_Value(std::forward<decltype(value)>(value));
      return detail::protect([&] {
        ::rb_const_set(
            as_VALUE(), detail::into_ID(std::forward<decltype(name)>(name)), v.as_VALUE());
      });
    }
  }

  inline Module convert::FromValue<Module>::convert(Value value) {
    auto const type = ::rb_type(value.as_VALUE());
    if(type != RUBY_T_MODULE && type != RUBY_T_CLASS) {
      rb_raise(rb_eTypeError, "Expected a Module but got a %s", rb_obj_classname(value.as_VALUE()));
    }
    return detail::unsafe_coerce<Module>{value.as_VALUE()};
  };

  /// Class

  namespace value {
    template <typename T>
    inline T ClassT<T>::new_instance(concepts::ConvertibleIntoValue auto &&...args) const
      requires std::derived_from<T, ValueBase>
    {
      std::array<VALUE, sizeof...(args)> vargs{
        into_Value(std::forward<decltype(args)>(args)).as_VALUE()...};
      return detail::unsafe_coerce<T>(detail::protect(
          [&] { return ::rb_class_new_instance(vargs.size(), vargs.data(), this->as_VALUE()); }));
    }

    template <typename T> inline Value ClassT<T>::allocate() const {
      return detail::unsafe_coerce<Value>(detail::protect(::rb_obj_alloc, this->as_VALUE()));
    }

    template <typename T>
    template <concepts::ArgSpec... ArgSpec>
    inline ClassT<T> ClassT<T>::define_method(concepts::Identifier auto &&mid,
        std::invocable<T &, typename ArgSpec::ResultType...> auto &&function, ArgSpec...) const {
      auto const callback =
          detail::method_callback<arg::Self<detail::self_type<T>>, ArgSpec...>::alloc(
              std::forward<decltype(function)>(function));
      detail::protect([&]() {
        rb_define_method_id(
            this->as_VALUE(), detail::into_ID(std::forward<decltype(mid)>(mid)), callback, -1);
      });
      return *this;
    }

    template <typename T>
    template <concepts::ArgSpec... ArgSpec>
    inline ClassT<T> ClassT<T>::define_method_const(concepts::Identifier auto &&mid,
        std::invocable<T const &, typename ArgSpec::ResultType...> auto &&function,
        ArgSpec...) const {
      auto const callback =
          detail::method_callback<arg::Self<detail::self_type_const<T>>, ArgSpec...>::alloc(
              function);
      detail::protect([&] {
        rb_define_method_id(
            this->as_VALUE(), detail::into_ID(std::forward<decltype(mid)>(mid)), callback, -1);
      });
      return *this;
    }

    template <typename T>
    template <concepts::ArgSpec... ArgSpec>
      requires std::constructible_from<T, typename ArgSpec::ResultType...>
    inline ClassT<T> ClassT<T>::define_constructor(ArgSpec...) const {
      auto const callback = detail::method_callback<arg::Self<Value>, ArgSpec...>::alloc(
          typed_data::DataType<T>::template initialize<typename ArgSpec::ResultType...>);
      detail::protect([&] {
        using namespace literals;
        rb_define_method_id(this->as_VALUE(), detail::into_ID("initialize"_id), callback, -1);
      });
      return *this;
    }

    template <typename T>
    inline ClassT<T> ClassT<T>::define_copy_constructor() const
      requires std::copy_constructible<T>
    {
      auto const callback = detail::method_callback<arg::Self<Value>, arg::Arg<T const &>>::alloc(
          typed_data::DataType<T>::initialize_copy);
      detail::protect([&] {
        using namespace literals;
        rb_define_method_id(this->as_VALUE(), detail::into_ID("initialize_copy"_id), callback, -1);
      });
      return *this;
    }

    template <typename T> inline Class ClassT<T>::new_class() {
      return new_class(builtin::Object);
    }
    template <typename T>
    template <typename S>
    inline ClassT<S> ClassT<T>::new_class(ClassT<S> superclass) {
      return detail::unsafe_coerce<ClassT<S>>(
          detail::protect(::rb_class_new, superclass.as_VALUE()));
    }
  }

  template <typename T> ClassT<T> convert::FromValue<ClassT<T>>::convert(Value value) {
    if(::rb_type(value.as_VALUE()) != RUBY_T_CLASS) {
      rb_raise(rb_eTypeError, "Expected a Class but got a %s", rb_obj_classname(value.as_VALUE()));
    }
    return detail::unsafe_coerce<ClassT<T>>{value.as_VALUE()};
  };

  /// Symbol

  namespace value {
    template <size_t N> inline Symbol::Symbol(char const (&s)[N]) noexcept: Symbol({&s[0], N - 1}) {
    }

    inline Symbol::Symbol(std::string_view sv) noexcept
        : Symbol(detail::protect([&] {
            return detail::assume_noexcept(::rb_to_symbol)(
                detail::assume_noexcept(::rb_interned_str)(sv.data(), sv.size()));
          })) {
    }

    inline ID Symbol::as_ID() const noexcept {
      return detail::protect(::rb_sym2id, as_VALUE());
    }
  }

  inline Symbol convert::FromValue<Symbol>::convert(Value value) {
    if(::rb_type(value.as_VALUE()) != RUBY_T_SYMBOL) {
      rb_raise(rb_eTypeError, "Expected a Symbol but got a %s", rb_obj_classname(value.as_VALUE()));
    }
    return detail::unsafe_coerce<Symbol>(value.as_VALUE());
  }

  /// String

  namespace value {
    template <concepts::StringLike S> inline String String::intern_from(S &&s) {
      using CharT = typename std::remove_cvref_t<S>::value_type;
      using Traits = typename std::remove_cvref_t<S>::traits_type;
      std::basic_string_view<CharT, Traits> sv(std::forward<S>(s));
      return detail::unsafe_coerce<String>(detail::protect([&] {
        return (::rb_enc_interned_str)(
            reinterpret_cast<char const *>(sv.data()), sv.size(), CharTraits<CharT>::encoding());
      }));
    }

    template <concepts::CharLike CharT>
    inline String String::intern_from(CharT const *RCX_Nonnull s) {
      return intern_from(std::basic_string_view<CharT>(s));
    }

    template <concepts::StringLike S> inline String String::copy_from(S &&s) {
      using CharT = typename std::remove_cvref_t<S>::value_type;
      using Traits = typename std::remove_cvref_t<S>::traits_type;
      std::basic_string_view<CharT, Traits> sv(std::forward<S>(s));
      return detail::unsafe_coerce<String>(detail::protect([&] {
        return (::rb_enc_str_new)(
            reinterpret_cast<char const *>(sv.data()), sv.size(), CharTraits<CharT>::encoding());
      }));
    }

    template <concepts::CharLike CharT>
    inline String String::copy_from(CharT const *RCX_Nonnull s) {
      return copy_from(std::basic_string_view<CharT>(s));
    }

    inline size_t String::size() const noexcept {
      return RSTRING_LEN(as_VALUE());
    }

    inline char *RCX_Nonnull String::data() const {
      detail::protect([&] { ::rb_check_frozen(as_VALUE()); });
      return RSTRING_PTR(as_VALUE());
    }

    inline char const *RCX_Nonnull String::cdata() const noexcept {
      return RSTRING_PTR(as_VALUE());
    }

    inline String::operator std::string_view() const noexcept {
      return {data(), size()};
    }

    inline String String::locktmp() const {
      return detail::unsafe_coerce<String>(detail::protect(::rb_str_locktmp, as_VALUE()));
    }

    inline String String::unlocktmp() const {
      return detail::unsafe_coerce<String>(detail::protect(::rb_str_unlocktmp, as_VALUE()));
    }
  }

  inline String convert::FromValue<String>::convert(Value value) {
    if(::rb_type(value.as_VALUE()) != RUBY_T_STRING) {
      rb_raise(rb_eTypeError, "Expected a String but got a %s", rb_obj_classname(value.as_VALUE()));
    }
    return detail::unsafe_coerce<String>(value.as_VALUE());
  }
  inline std::string_view convert::FromValue<std::string_view>::convert(Value value) {
    return std::string_view(from_Value<String>(value));
  }

  /// Proc

  namespace value {
    inline bool Proc::is_lambda() const {
      Value const v = detail::unsafe_coerce<Value>(
          detail::protect([&] { return ::rb_proc_lambda_p(as_VALUE()); }));
      return v.test();
    }

    inline Value Proc::call(Array args) const {
      return detail::unsafe_coerce<Value>(
          detail::protect([&] { return ::rb_proc_call(as_VALUE(), args.as_VALUE()); }));
    }
  }

  namespace convert {
    inline Proc convert::FromValue<Proc>::convert(Value value) {
      if(!rb_obj_is_proc(value.as_VALUE())) {
        rb_raise(rb_eTypeError, "Expected a Proc but got a %s", rb_obj_classname(value.as_VALUE()));
      }
      return detail::unsafe_coerce<Proc>{value.as_VALUE()};
    };
  }

#ifdef RCX_IO_BUFFER
  /// IOBuffer
  namespace value {

    inline IOBuffer IOBuffer::new_internal(size_t size) {
      return detail::unsafe_coerce<IOBuffer>(detail::protect([size] {
        // Let Ruby allocate a buffer
        return ::rb_io_buffer_new(nullptr, size, RB_IO_BUFFER_INTERNAL);
      }));
    }

    inline IOBuffer IOBuffer::new_mapped(size_t size) {
      return detail::unsafe_coerce<IOBuffer>(detail::protect([size] {
        // Let Ruby allocate a buffer
        return ::rb_io_buffer_new(nullptr, size, RB_IO_BUFFER_MAPPED);
      }));
    }

    template <size_t N> inline IOBuffer IOBuffer::new_external(std::span<std::byte, N> bytes) {
      return detail::unsafe_coerce<IOBuffer>(detail::protect([bytes] {
        return ::rb_io_buffer_new(bytes.data(), bytes.size(), RB_IO_BUFFER_EXTERNAL);
      }));
    }

    template <size_t N>
    inline IOBuffer IOBuffer::new_external(std::span<std::byte const, N> bytes) {
      return detail::unsafe_coerce<IOBuffer>(detail::protect([bytes] {
        return ::rb_io_buffer_new(const_cast<std::byte *>(bytes.data()), bytes.size(),
            static_cast<rb_io_buffer_flags>(RB_IO_BUFFER_EXTERNAL | RB_IO_BUFFER_READONLY));
      }));
    }

    inline void IOBuffer::free() const {
      detail::protect([this] { ::rb_io_buffer_free(as_VALUE()); });
    }

    inline void IOBuffer::resize(size_t size) const {
      detail::protect([this, size] { ::rb_io_buffer_resize(as_VALUE(), size); });
    }

    inline std::span<std::byte> IOBuffer::bytes() const {
      void *ptr;
      size_t size;
      detail::protect([&] { ::rb_io_buffer_get_bytes_for_writing(as_VALUE(), &ptr, &size); });
      return {static_cast<std::byte *>(ptr), size};
    }

    inline std::span<std::byte const> IOBuffer::cbytes() const {
      void const *ptr;
      size_t size;
      detail::protect([&] { ::rb_io_buffer_get_bytes_for_reading(as_VALUE(), &ptr, &size); });
      return {static_cast<std::byte const *>(ptr), size};
    }

    inline void IOBuffer::lock() const {
      detail::protect([this] { ::rb_io_buffer_lock(as_VALUE()); });
    }

    inline void IOBuffer::unlock() const {
      detail::protect([this] { ::rb_io_buffer_unlock(as_VALUE()); });
    }

    inline bool IOBuffer::try_lock() const {
      try {
        lock();
        return true;
      } catch(RubyError const &) {
        return false;
      }
    }
  }

  namespace convert {
    inline IOBuffer FromValue<IOBuffer>::convert(Value value) {
      if(!rb_obj_is_kind_of(value.as_VALUE(), rb_cIOBuffer)) {
        rb_raise(
            rb_eTypeError, "Expected an IOBuffer but got a %s", rb_obj_classname(value.as_VALUE()));
      }
      return detail::unsafe_coerce<IOBuffer>(value.as_VALUE());
    }
  }
#endif

  /// Pinned

  namespace value {
    template <std::derived_from<ValueBase> T> inline PinnedOpt<T>::Storage::Storage(T v): value{v} {
      ::rb_gc_register_address(const_cast<VALUE *>(reinterpret_cast<VALUE const *>(&value)));
    }

    template <std::derived_from<ValueBase> T> inline PinnedOpt<T>::Storage::~Storage() {
      ::rb_gc_unregister_address(const_cast<VALUE *>(reinterpret_cast<VALUE const *>(&value)));
    }

    template <std::derived_from<ValueBase> T>
    inline PinnedOpt<T>::PinnedOpt(T value): ptr_(std::make_shared<Storage>(value)) {
    }

    template <std::derived_from<ValueBase> T> inline T &PinnedOpt<T>::operator*() const noexcept {
      return ptr_->value;
    }

    template <std::derived_from<ValueBase> T>
    inline T *RCX_Nullable PinnedOpt<T>::operator->() const noexcept {
      rcx_assert(ptr_);
      return &ptr_->value;
    }

    template <std::derived_from<ValueBase> T> inline PinnedOpt<T>::operator bool() const noexcept {
      return ptr_;
    }

    template <std::derived_from<ValueBase> T>
    inline Pinned<T>::Pinned(T value): PinnedOpt<T>(value) {
    }

    template <std::derived_from<ValueBase> T>
    inline T *RCX_Nonnull Pinned<T>::operator->() const noexcept {
      return PinnedOpt<T>::operator->();
    }
  }

  /// Array

  namespace value {
    inline size_t Array::size() const noexcept {
      return rb_array_len(as_VALUE());
    }

    template <concepts::ConvertibleFromValue T> inline decltype(auto) Array::at(size_t i) const {
      return from_Value<T>((*this)[i]);
    }

    inline Value Array::operator[](size_t i) const {
      VALUE const index = RB_SIZE2NUM(i);
      return detail::unsafe_coerce<Value>(
          detail::protect([&] { return ::rb_ary_aref(1, &index, as_VALUE()); }));
      ;
    }

    template <std::ranges::contiguous_range R>
#ifdef HAVE_STD_IS_LAYOUT_COMPATIBLE
      requires std::is_layout_compatible_v<std::ranges::range_value_t<R>, ValueBase>
#else
      requires(std::derived_from<std::ranges::range_value_t<R>, ValueBase> &&
               sizeof(std::ranges::range_value_t<R>) == sizeof(ValueBase))
#endif
    inline Array Array::new_from(R const &elements) {
      // contiguous_range<T> has a layout combatible to VALUE[]
      return detail::unsafe_coerce<Array>(detail::protect([&] {
        return ::rb_ary_new_from_values(
            elements.size(), reinterpret_cast<VALUE const *>(elements.data()));
      }));
    };

    inline Array Array::new_from(std::initializer_list<ValueBase> elements) {
      return detail::unsafe_coerce<Array>(detail::protect([&] {
        return ::rb_ary_new_from_values(
            elements.size(), reinterpret_cast<VALUE const *>(elements.begin()));
      }));
    }

    template <std::derived_from<ValueBase>... T>
    inline Array Array::new_from(std::tuple<T...> const &elements) {
      return detail::unsafe_coerce<Array>(detail::protect([&] {
        return std::apply(
            [](auto... v) { return ::rb_ary_new_from_args(sizeof...(v), v.as_VALUE()...); },
            elements);
      }));
    }

    inline Array Array::new_array() {
      return detail::unsafe_coerce<Array>(detail::protect([] { return ::rb_ary_new(); }));
    }

    inline Array Array::new_array(long capacity) {
      return detail::unsafe_coerce<Array>(
          detail::protect([capacity] { return ::rb_ary_new_capa(capacity); }));
    }

    template <concepts::ConvertibleIntoValue T> Array Array::push_back(T value) const {
      auto const v = into_Value<T>(value);
      detail::protect([v, this] { ::rb_ary_push(as_VALUE(), v.as_VALUE()); });
      return *this;
    }

    template <concepts::ConvertibleFromValue T> inline T Array::pop_back() const {
      return from_Value<T>(detail::unsafe_coerce<Value>(
          detail::protect([this] { return ::rb_ary_pop(as_VALUE()); })));
    }

    template <concepts::ConvertibleIntoValue T> Array Array::push_front(T value) const {
      auto const v = into_Value<T>(value);
      detail::protect([v, this] { ::rb_ary_unshift(as_VALUE(), v.as_VALUE()); });
      return *this;
    }

    template <concepts::ConvertibleFromValue T> inline T Array::pop_front() const {
      return from_Value<T>(detail::unsafe_coerce<Value>(
          detail::protect([this] { return ::rb_ary_shift(as_VALUE()); })));
    }
  }

  inline Array convert::FromValue<Array>::convert(Value value) {
    if(::rb_type(value.as_VALUE()) != RUBY_T_ARRAY) {
      rb_raise(rb_eTypeError, "Expected an Array but got a %s", rb_obj_classname(value.as_VALUE()));
    }
    return detail::unsafe_coerce<Array>(value.as_VALUE());
  }

  template <concepts::ConvertibleFromValue T>
  decltype(auto) FromValue<std::optional<T>>::convert(Value v) {
    return v.is_nil() ? std::nullopt : from_Value<T>(v);
  }

  template <concepts::ConvertibleIntoValue T>
  Value IntoValue<std::optional<T>>::convert(std::optional<T> value) {
    return value ? Value::qnil : into_Value(*value);
  }

  template <concepts::ConvertibleFromValue... T>
  inline decltype(auto) convert::FromValue<std::tuple<T...>>::convert(Value value) {
    auto array = from_Value<Array>(value);
    if(array.size() != sizeof...(T)) {
      throw RubyError::format(
          builtin::ArgumentError, "Array of length {} is expected", sizeof...(T));
    }
    return [array]<size_t... I>(std::index_sequence<I...>) {
      return std::make_tuple(array.at<T>(I)...);
    }(std::make_index_sequence<sizeof...(T)>());
  }

  template <concepts::ConvertibleIntoValue... T>
  Value IntoValue<std::tuple<T...>>::convert(std::tuple<T...> value) {
    return [&value]<size_t... I>(std::index_sequence<I...>) {
      return Array::new_from(
          std::tuple{into_Value<decltype(std::get<I>(value))>(std::get<I>(value))...});
    }(std::make_index_sequence<sizeof...(T)>());
  };

  /// Misc

  namespace literals {
    template <detail::cxstring s> String operator""_str() {
      return String::copy_from(s);
    }

    template <detail::u8cxstring s> String operator""_str() {
      return String::copy_from(s);
    }

    template <detail::cxstring s> String operator""_fstr() {
      static String str = detail::unsafe_coerce<String>(detail::protect(
          [&] { return ::rb_obj_freeze(::rb_str_new_static(s.data(), s.size())); }));
      return str;
    }

    template <detail::u8cxstring s> String operator""_fstr() {
      static String str = detail::unsafe_coerce<String>(detail::protect([&] {
        return ::rb_obj_freeze(::rb_enc_str_new_static(
            reinterpret_cast<char const *>(s.data()), s.size(), rb_utf8_encoding()));
      }));
      return str;
    }

    template <detail::cxstring s> Symbol operator""_sym() {
      static Symbol sym = detail::unsafe_coerce<Symbol>(
          detail::protect([&] { return ::rb_id2sym(operator""_id < s>().as_ID()); }));
      return sym;
    }

    template <detail::u8cxstring s> Symbol operator""_sym() {
      static Symbol sym = detail::unsafe_coerce<Symbol>(
          detail::protect([&] { return ::rb_id2sym(operator""_id < s>().as_VALUE()); }));
      return sym;
    }

    template <detail::cxstring s> Id operator""_id() {
      static Id const id{detail::protect([&] { return ::rb_intern2(s.data(), s.size()); })};
      return id;
    }

    template <detail::u8cxstring s> Id operator""_id() {
      static Id const id{
        detail::protect([&] { return ::rb_intern_str(operator""_fstr < s>().as_VALUE()); })};
      return id;
    }

  }

  /// Ruby

  inline Module Ruby::define_module(concepts::Identifier auto &&name) {
    return builtin::Object.define_module(std::forward<decltype(name)>(name));
  }

  template <typename T, typename S>
  inline ClassT<T> Ruby::define_class(concepts::Identifier auto &&name, ClassT<S> superclass) {
    return builtin::Object.define_class<T>(std::forward<decltype(name)>(name), superclass);
  }

  template <typename T> inline ClassT<T> Ruby::define_class(concepts::Identifier auto &&name) {
    return define_class<T>(std::forward<decltype(name)>(name), builtin::Object);
  }

  inline RubyError::RubyError(Value exception) noexcept: exception_(exception) {
  }

  inline Value RubyError::exception() const noexcept {
    return exception_;
  }

  template <typename... Args>
  inline RubyError RubyError::format(Class cls, std::format_string<Args...> fmt, Args &&...args) {
    auto const msg = std::format(fmt, std::forward<Args>(args)...);
    return RubyError{cls.new_instance(String::intern_from(msg))};
  }

  namespace detail {
    inline std::string demangle_type_info(std::type_info const &ti) {
      if constexpr(have_abi_cxa_demangle) {
        std::unique_ptr<char, decltype(&free)> const name = {
          abi::__cxa_demangle(ti.name(), nullptr, 0, nullptr),
          free,
        };
        return name.get();
      }
      return {};
    }

    inline Value make_ruby_exception(
        std::exception const *RCX_Nullable exc, std::type_info const *RCX_Nullable ti) {
      std::string name, msg;
      if(ti)
        name = demangle_type_info(*ti);
      if(exc)
        msg = exc->what();
      return builtin::RuntimeError.new_instance(String::copy_from(
          std::format("{}: {}", name.empty() ? std::string{"unknown"} : name, msg)));
    }

    inline auto cxx_protect(std::invocable<> auto const &functor) noexcept
        -> std::invoke_result_t<decltype(functor)> {
      try {
        return functor();
      } catch(Jump const &jump) {
        ::rb_jump_tag(jump.state);
      } catch(RubyError const &exc) {
        ::rb_exc_raise(exc.exception().as_VALUE());
      } catch(std::exception const &exc) {
        ::rb_exc_raise(make_ruby_exception(&exc, &typeid(exc)).as_VALUE());
      } catch(...) {
        if constexpr(have_abi_cxa_current_exception_type) {
          ::rb_exc_raise(
              make_ruby_exception(nullptr, abi::__cxa_current_exception_type()).as_VALUE());
        } else {
          ::rb_exc_raise(make_ruby_exception(nullptr, nullptr).as_VALUE());
        }
      }
    }
  }
}

namespace std {
  template <std::derived_from<rcx::Value> T>
  template <typename ParseContext>
  constexpr ParseContext::iterator formatter<T, char>::parse(ParseContext &ctx) {
    auto it = ctx.begin();
    if(it == ctx.end()) {
      return it;
    }
    if(*it == '#') {
      inspect = true;
      ++it;
    }
    if(it == ctx.end() || *it != '}') {
      throw std::format_error("Invalid format args for std::formatter<ValueBase>.");
    }
    return it;
  }

  template <std::derived_from<rcx::Value> T>
  template <typename FormatContext>
  FormatContext::iterator formatter<T, char>::format(T value, FormatContext &ctx) const {
    return std::format_to(ctx.out(), "{}",
        static_cast<std::string_view>(inspect ? value.inspect() : value.to_string()));
  }
}