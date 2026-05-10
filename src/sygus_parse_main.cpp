#include "sygus_parser.hpp"

#include <fstream>
#include <iostream>
#include <string>

namespace {

void printUsage() {
  std::cout << "Usage: sygus_parse [--parse-only] [--dump-ast] <input-file>\n";
}

void printSummary(const SyGuSProgram& program) {
  std::cout << "logic: " << program.logic << "\n";
  std::cout << "synth-funs: " << program.synth_funs.size() << "\n";
  std::cout << "declare-vars: " << program.declare_vars.size() << "\n";
  std::cout << "declare-primed-vars: " << program.declare_primed_vars.size()
            << "\n";
  std::cout << "define-funs: " << program.define_funs.size() << "\n";
  std::cout << "synth-invs: " << program.synth_invs.size() << "\n";
  std::cout << "constraints: " << program.constraints.size() << "\n";
  std::cout << "inv-constraints: " << program.inv_constraints.size() << "\n";
  std::cout << "check-synth: " << (program.has_check_synth ? "yes" : "no")
            << "\n";
  std::cout << "other-commands: " << program.other_commands.size() << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  bool parse_only = false;
  bool dump_ast = false;
  std::string input_file;

  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--parse-only") {
      parse_only = true;
    } else if (argument == "--dump-ast") {
      dump_ast = true;
    } else if (argument == "--help" || argument == "-h") {
      printUsage();
      return 0;
    } else if (input_file.empty()) {
      input_file = argument;
    } else {
      std::cerr << "Unexpected argument: " << argument << "\n";
      printUsage();
      return 2;
    }
  }

  if (input_file.empty()) {
    printUsage();
    return 2;
  }

  try {
    SyGuSParser parser;
    const SyGuSProgram program = parser.parseFile(input_file);

    if (dump_ast) {
      for (const auto& form : program.top_level_forms) {
        std::cout << form.toString() << "\n";
      }
      return 0;
    }

    if (!parse_only) {
      printSummary(program);
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Parse failed: " << error.what() << "\n";
    return 1;
  }
}
