#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "sygus_ast.hpp"

class SyGuSParser {
 public:
  SyGuSProgram parse(const std::string& input) const;
  SyGuSProgram parseFile(const std::filesystem::path& path) const;
  std::vector<SExpr> parseForms(const std::string& input) const;
};
