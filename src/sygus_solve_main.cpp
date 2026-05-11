#include "sygus_parser.hpp"
#include "sygus_solver.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

void printUsage() {
  std::cout << "Usage: sygus_solve [--json] [--max-expression-size N] "
               "[--max-candidates N] [--max-cegis-rounds N] [--max-samples N] "
               "[--no-cvc5-verify] [--model PATH] "
               "[--verbose] [--strategy enum|best-first|ga|sa] "
               "[--ga-population N] [--ga-generations N] [--sa-steps N] "
               "<input-file>\n";
}

bool parseSize(const std::string& text, size_t& output) {
  std::istringstream stream(text);
  stream >> output;
  return stream && stream.eof();
}

std::string escapeJson(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const char character : value) {
    switch (character) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += character;
        break;
    }
  }
  return escaped;
}

void printJson(const SolveResult& result) {
  std::cout << "{\n";
  std::cout << "  \"status\": \""
            << escapeJson(SyGuSSolver::statusToString(result.status))
            << "\",\n";
  std::cout << "  \"logic\": \"" << escapeJson(result.logic) << "\",\n";
  std::cout << "  \"message\": \"" << escapeJson(result.message) << "\",\n";
  std::cout << "  \"solution\": \"" << escapeJson(result.solution) << "\",\n";
  std::cout << "  \"define_fun\": \"" << escapeJson(result.define_fun)
            << "\",\n";
  std::cout << "  \"enumerated_candidates\": " << result.enumerated_candidates
            << ",\n";
  std::cout << "  \"ml_filtered_candidates\": " << result.ml_filtered_candidates
            << ",\n";
  std::cout << "  \"tested_candidates\": " << result.tested_candidates << ",\n";
  std::cout << "  \"cegis_rounds\": " << result.cegis_rounds << ",\n";
  std::cout << "  \"counterexamples_found\": " << result.counterexamples_found
            << ",\n";
  std::cout << "  \"cvc5_available\": "
            << (result.cvc5_available ? "true" : "false") << ",\n";
  std::cout << "  \"cvc5_verified\": "
            << (result.cvc5_verified ? "true" : "false") << ",\n";
  std::cout << "  \"strategy\": \"" << escapeJson(result.strategy_name)
            << "\",\n";
  std::cout << "  \"ga_generations_used\": " << result.ga_generations_used
            << "\n";
  std::cout << "}\n";
}

void printText(const SolveResult& result) {
  std::cout << "status: " << SyGuSSolver::statusToString(result.status) << "\n";
  std::cout << "logic: " << result.logic << "\n";
  std::cout << "cvc5-available: " << (result.cvc5_available ? "yes" : "no")
            << "\n";
  std::cout << "cvc5-verified: " << (result.cvc5_verified ? "yes" : "no")
            << "\n";
  std::cout << "enumerated-candidates: " << result.enumerated_candidates
            << "\n";
  std::cout << "ml-filtered-candidates: " << result.ml_filtered_candidates
            << "\n";
  std::cout << "tested-candidates: " << result.tested_candidates << "\n";
  std::cout << "cegis-rounds: " << result.cegis_rounds << "\n";
  std::cout << "counterexamples-found: " << result.counterexamples_found
            << "\n";
  std::cout << "strategy: " << result.strategy_name << "\n";
  if (result.ga_generations_used > 0) {
    std::cout << "ga-generations-used: " << result.ga_generations_used << "\n";
  }
  if (!result.message.empty()) {
    std::cout << "message: " << result.message << "\n";
  }
  if (!result.define_fun.empty()) {
    std::cout << result.define_fun << "\n";
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  bool print_json = false;
  std::string input_file;
  SolveOptions options;

  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--json") {
      print_json = true;
    } else if (argument == "--verbose" || argument == "-v") {
      options.verbose = true;
    } else if (argument == "--no-cvc5-verify") {
      options.require_cvc5_verification = false;
    } else if (argument == "--max-expression-size") {
      if (index + 1 >= argc ||
          !parseSize(argv[index + 1], options.max_expression_size)) {
        std::cerr << "Invalid value for --max-expression-size\n";
        return 2;
      }
      ++index;
    } else if (argument == "--max-candidates") {
      if (index + 1 >= argc ||
          !parseSize(argv[index + 1], options.max_candidates)) {
        std::cerr << "Invalid value for --max-candidates\n";
        return 2;
      }
      ++index;
    } else if (argument == "--max-cegis-rounds") {
      if (index + 1 >= argc ||
          !parseSize(argv[index + 1], options.max_cegis_rounds)) {
        std::cerr << "Invalid value for --max-cegis-rounds\n";
        return 2;
      }
      ++index;
    } else if (argument == "--max-samples") {
      if (index + 1 >= argc ||
          !parseSize(argv[index + 1], options.max_sample_assignments)) {
        std::cerr << "Invalid value for --max-samples\n";
        return 2;
      }
      ++index;
    } else if (argument == "--model") {
      if (index + 1 >= argc) {
        std::cerr << "Missing value for --model\n";
        return 2;
      }
      options.model_path = argv[++index];
    } else if (argument == "--strategy") {
      if (index + 1 >= argc) {
        std::cerr << "Missing value for --strategy\n";
        return 2;
      }
      std::string strategy_name = argv[++index];
      if (strategy_name == "enum") {
        options.strategy = SearchStrategy::Enum;
      } else if (strategy_name == "best-first") {
        options.strategy = SearchStrategy::BestFirst;
      } else if (strategy_name == "ga") {
        options.strategy = SearchStrategy::GA;
      } else if (strategy_name == "sa") {
        options.strategy = SearchStrategy::SA;
      } else {
        std::cerr << "Unknown strategy: " << strategy_name
                  << " (use enum, best-first, ga, or sa)\n";
        return 2;
      }
    } else if (argument == "--ga-population") {
      if (index + 1 >= argc ||
          !parseSize(argv[index + 1], options.ga_population_size)) {
        std::cerr << "Invalid value for --ga-population\n";
        return 2;
      }
      ++index;
    } else if (argument == "--ga-generations") {
      if (index + 1 >= argc ||
          !parseSize(argv[index + 1], options.ga_generations)) {
        std::cerr << "Invalid value for --ga-generations\n";
        return 2;
      }
      ++index;
    } else if (argument == "--sa-steps") {
      if (index + 1 >= argc ||
          !parseSize(argv[index + 1], options.sa_max_steps)) {
        std::cerr << "Invalid value for --sa-steps\n";
        return 2;
      }
      ++index;
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
    SyGuSSolver solver;
    const SolveResult result = solver.solve(program, options);

    if (print_json) {
      printJson(result);
    } else {
      printText(result);
    }

    return result.status == SolveResult::Status::Error ? 1 : 0;
  } catch (const std::exception& error) {
    if (print_json) {
      SolveResult failure;
      failure.status = SolveResult::Status::Error;
      failure.message = error.what();
      printJson(failure);
    } else {
      std::cerr << "Solve failed: " << error.what() << "\n";
    }
    return 1;
  }
}
