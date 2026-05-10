#pragma once

#include <cstddef>
#include <string>

#include "sygus_parser.hpp"

struct SolveOptions {
  size_t max_expression_size = 6;
  size_t max_candidates = 5000;
  size_t max_sample_assignments = 128;
  bool require_cvc5_verification = true;
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
  size_t tested_candidates = 0;
  bool cvc5_available = false;
  bool cvc5_verified = false;
};

class SyGuSSolver {
 public:
  SolveResult solve(const SyGuSProgram& program,
                    const SolveOptions& options = {}) const;

  static bool hasCvc5();
  static std::string statusToString(SolveResult::Status status);
};
