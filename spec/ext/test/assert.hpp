// SPDX-License-Identifier: BSL-1.0
// SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>
#pragma once

#define ASSERT(V)                                                                                  \
  [](auto value) {                                                                                 \
    if(!value)                                                                                     \
      rb_raise(                                                                                    \
          rb_eExpectationNotMetError(), "%s:%d: Expected to satisfy: %s", __FILE__, __LINE__, #V); \
  }((V))
#define ASSERT_NOT(V)                                                                              \
  [](auto value) {                                                                                 \
    if(value)                                                                                      \
      rb_raise(rb_eExpectationNotMetError(), "%s:%d: Expected not to satisfy: %s", __FILE__,       \
          __LINE__, #V);                                                                           \
  }((V))
#define ASSERT_EQ(EXP, V)                                                                          \
  [](auto expected, auto value) {                                                                  \
    if(expected != value)                                                                          \
      rb_raise(rb_eExpectationNotMetError(), "%s:%d: Expected %s to equal to: %s", __FILE__,       \
          __LINE__, #EXP, #V);                                                                     \
  }((EXP), (V))
#define ASSERT_NEQ(EXP, V)                                                                         \
  [](auto expected, auto value) {                                                                  \
    if(expected == value)                                                                          \
      rb_raise(rb_eExpectationNotMetError(), "%s:%d: Expected %s not to equal to: %s", __FILE__,   \
          __LINE__, #EXP, #V);                                                                     \
  }((EXP), (V))
#define ASSERT_RAISE(V)                                                                            \
  [](auto F) {                                                                                     \
    try {                                                                                          \
      F();                                                                                         \
    } catch(rcx::value::Exception const &e) {                                                      \
      return;                                                                                      \
    }                                                                                              \
    rb_raise(rb_eExpectationNotMetError(), "%s:%d: Expected %s to raise", __FILE__, __LINE__, #V); \
  }((V))
