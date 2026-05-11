#pragma once

#include <cstddef>
#include <string>

#include "sygus_parser.hpp"

enum class SearchStrategy {
  Enum,
  BestFirst,
  GA,
  SA,
};

struct SolveOptions {
  size_t max_expression_size = 8;
  size_t max_candidates = 50000;
  size_t max_cegis_rounds = 200;
  size_t max_sample_assignments = 128;
  bool require_cvc5_verification = true;
  std::string model_path;
  SearchStrategy strategy = SearchStrategy::Enum;
  size_t ga_population_size = 200;
  size_t ga_generations = 500;
  double ga_mutation_rate = 0.3;
  double ga_crossover_rate = 0.7;
  double sa_initial_temp = 1000.0;
  double sa_cooling_rate = 0.995;
  size_t sa_max_steps = 10000;
  bool verbose = false;
};

struct SolveResult {
  enum class Status {
    Solved,
    Unsupported,
    Exhausted,
    Error,
  };

  Status status = Status::Error;
  std::string message;
  std::string logic;
  std::string solution;
  std::string define_fun;
  size_t enumerated_candidates = 0;
  size_t ml_filtered_candidates = 0;
  size_t tested_candidates = 0;
  size_t cegis_rounds = 0;
  size_t counterexamples_found = 0;
  bool cvc5_available = false;
  bool cvc5_verified = false;
  std::string strategy_name;
  size_t ga_generations_used = 0;
};

class SyGuSSolver {
 public:
  SolveResult solve(const SyGuSProgram& program,
                    const SolveOptions& options = {}) const;

  static bool hasCvc5();
  static std::string statusToString(SolveResult::Status status);
};
