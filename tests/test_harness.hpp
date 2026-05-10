#pragma once

#include <iostream>
#include <sstream>
#include <string>

struct TestContext {
  int failures = 0;

  void fail(const std::string& expression, const std::string& file, int line,
            const std::string& message) {
    ++failures;
    std::cerr << file << ":" << line << ": check failed: " << expression;
    if (!message.empty()) {
      std::cerr << " -- " << message;
    }
    std::cerr << "\n";
  }
};

template <typename Left, typename Right>
void expectEqual(TestContext& context, const Left& left, const Right& right,
                 const char* left_text, const char* right_text,
                 const char* file, int line) {
  if (!(left == right)) {
    std::ostringstream message;
    message << left_text << " != " << right_text;
    context.fail(left_text, file, line, message.str());
  }
}

#define EXPECT_TRUE(context, expression)                                \
  do {                                                                  \
    if (!(expression)) {                                                \
      (context).fail(#expression, __FILE__, __LINE__, "expected true"); \
    }                                                                   \
  } while (false)

#define EXPECT_EQ(context, left, right)                              \
  do {                                                               \
    expectEqual((context), (left), (right), #left, #right, __FILE__, \
                __LINE__);                                           \
  } while (false)
