#include "sygus_parser.hpp"

#include <string>

#include "test_harness.hpp"

int main() {
  TestContext context;
  SyGuSParser parser;

  {
    const auto forms =
        parser.parseForms("; ignore me\n(set-logic LIA)\n(check-synth)\n");
    EXPECT_EQ(context, forms.size(), static_cast<size_t>(2));
    EXPECT_EQ(context, forms[0].toString(), std::string("(set-logic LIA)"));
    EXPECT_EQ(context, forms[1].toString(), std::string("(check-synth)"));
  }

  {
    const auto forms =
        parser.parseForms("(constraint (= (f x) (ite (> x 0) x 0)))");
    EXPECT_EQ(context, forms.size(), static_cast<size_t>(1));
    EXPECT_EQ(context, forms[0].toString(),
              std::string("(constraint (= (f x) (ite (> x 0) x 0)))"));
  }

  {
    const auto program = parser.parse(
        "(set-logic BV)\n"
        "(declare-var x (_ BitVec 8))\n"
        "(declare-primed-var x! (_ BitVec 8))\n"
        "(constraint (= x x!))\n"
        "(check-synth)\n");
    EXPECT_EQ(context, program.logic, std::string("BV"));
    EXPECT_EQ(context, program.declare_vars.size(), static_cast<size_t>(1));
    EXPECT_EQ(context, program.declare_vars[0].type,
              std::string("(_ BitVec 8)"));
    EXPECT_EQ(context, program.declare_primed_vars.size(),
              static_cast<size_t>(1));
    EXPECT_EQ(context, program.constraints.size(), static_cast<size_t>(1));
    EXPECT_TRUE(context, program.has_check_synth);
  }

  {
    const auto program = parser.parse(
        "(set-logic LIA)\n"
        "(synth-fun f ((x Int)) Int ((Start Int (x 0 1 (+ Start Start)))) )\n"
        "(check-synth)\n");
    EXPECT_EQ(context, program.synth_funs.size(), static_cast<size_t>(1));
    EXPECT_EQ(context, program.synth_funs[0].grammar_rules.size(),
              static_cast<size_t>(1));
    const auto& productions = program.synth_funs[0].grammar.at("Start");
    EXPECT_EQ(context, productions.size(), static_cast<size_t>(4));
    EXPECT_EQ(context, productions[3][0]->symbol, std::string("+"));
    EXPECT_EQ(context, productions[3][1]->symbol, std::string("Start"));
  }

  return context.failures == 0 ? 0 : 1;
}
