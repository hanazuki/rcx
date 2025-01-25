// SPDX-License-Identifier: BSL-1.0
// SPDX-FileCopyrightText: Copyright 2024-2025 Kasumi Hanazuki <kasumi@rollingapple.net>
#pragma once

#define ASSERT(V)                                                                                  \
  [](auto value) {                                                                                 \
    if(!value)                                                                                     \
      throw rcx::Exception::format(                                                                \
          rb_eExpectationNotMetError(), "{}:{}: Expected to satisfy: {}", __FILE__, __LINE__, #V); \
  }((V))
#define ASSERT_NOT(V)                                                                              \
  [](auto value) {                                                                                 \
    if(value)                                                                                      \
      throw rcx::Exception::format(rb_eExpectationNotMetError(),                                   \
          "{}:{}: Expected not to satisfy: {}", __FILE__, __LINE__, #V);                           \
  }((V))
#define ASSERT_EQ(EXP, V)                                                                          \
  [](auto expected, auto value) {                                                                  \
    if(expected != value)                                                                          \
      throw rcx::Exception::format(rb_eExpectationNotMetError(),                                   \
          "{}:{}: Expected {} to equal to: {}", __FILE__, __LINE__, #EXP, #V);                     \
  }((EXP), (V))
#define ASSERT_NEQ(EXP, V)                                                                         \
  [](auto expected, auto value) {                                                                  \
    if(expected == value)                                                                          \
      throw rcx::Exception::format(rb_eExpectationNotMetError(),                                   \
          "{}:{}: Expected {} not to equal to: {}", __FILE__, __LINE__, #EXP, #V);                 \
  }((EXP), (V))
#define ASSERT_RAISE(V)                                                                            \
  [](auto F) {                                                                                     \
    try {                                                                                          \
      F();                                                                                         \
    } catch(rcx::value::Exception const &e) {                                                      \
      return;                                                                                      \
    }                                                                                              \
    throw rcx::Exception::format(                                                                  \
        rb_eExpectationNotMetError(), "{}:{}: Expected {} to raise", __FILE__, __LINE__, #V);      \
  }((V))
