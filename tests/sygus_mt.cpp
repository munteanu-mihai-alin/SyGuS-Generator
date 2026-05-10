#include "sygus_parser.hpp"

#include <filesystem>
#include <string>

#include "test_harness.hpp"

namespace {

std::filesystem::path sourceRoot() {
  return std::filesystem::path(SYGUS_SOURCE_DIR);
}

}  // namespace

int main() {
  TestContext context;
  SyGuSParser parser;

  {
    const auto program =
        parser.parseFile(sourceRoot() / "tests" / "data" / "lia_max2.sl");
    EXPECT_EQ(context, program.logic, std::string("LIA"));
    EXPECT_EQ(context, program.synth_funs.size(), static_cast<size_t>(1));
    EXPECT_EQ(context, program.declare_vars.size(), static_cast<size_t>(2));
    EXPECT_EQ(context, program.constraints.size(), static_cast<size_t>(3));
    EXPECT_TRUE(context, program.has_check_synth);
    EXPECT_EQ(context, program.synth_funs[0].return_type, std::string("Int"));
  }

  {
    const auto program =
        parser.parseFile(sourceRoot() / "tests" / "data" / "bv_double.sl");
    EXPECT_EQ(context, program.logic, std::string("BV"));
    EXPECT_EQ(context, program.synth_funs.size(), static_cast<size_t>(1));
    EXPECT_EQ(context, program.synth_funs[0].return_type,
              std::string("(_ BitVec 8)"));
    EXPECT_EQ(context, program.constraints.size(), static_cast<size_t>(1));
  }

  {
    const auto program = parser.parseFile(sourceRoot() / "tests" / "data" /
                                          "invariant_with_primed.sl");
    EXPECT_EQ(context, program.logic, std::string("LIA"));
    EXPECT_EQ(context, program.synth_invs.size(), static_cast<size_t>(1));
    EXPECT_EQ(context, program.declare_primed_vars.size(),
              static_cast<size_t>(2));
    EXPECT_EQ(context, program.define_funs.size(), static_cast<size_t>(3));
    EXPECT_EQ(context, program.inv_constraints.size(), static_cast<size_t>(1));
    EXPECT_TRUE(context, program.has_check_synth);
  }

  return context.failures == 0 ? 0 : 1;
}
