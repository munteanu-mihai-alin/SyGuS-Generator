#include "sygus_solver.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#if SYGUS_HAVE_CVC5
#include <cvc5/cvc5.h>
#endif

namespace {

enum class ValueKind {
  Int,
  Bool,
  BitVec,
};

struct SortDescriptor {
  ValueKind kind = ValueKind::Int;
  uint32_t bit_width = 0;

  std::string toString() const {
    switch (kind) {
      case ValueKind::Int:
        return "Int";
      case ValueKind::Bool:
        return "Bool";
      case ValueKind::BitVec:
        return "(_ BitVec " + std::to_string(bit_width) + ")";
    }
    return "Unknown";
  }
};

struct BitVectorValue {
  uint64_t value = 0;
  uint32_t width = 0;

  std::string toString() const {
    std::ostringstream output;
    output << "#x";
    const uint32_t digits = std::max<uint32_t>(1, (width + 3) / 4);
    output.setf(std::ios::hex, std::ios::basefield);
    output.width(static_cast<std::streamsize>(digits));
    output.fill('0');
    output << value;
    return output.str();
  }
};

struct Value {
  std::variant<int64_t, bool, BitVectorValue> data;

  Value() : data(int64_t{0}) {}

  static Value makeInt(int64_t value) { return Value{value}; }

  static Value makeBool(bool value) { return Value{value}; }

  static Value makeBitVector(uint64_t value, uint32_t width) {
    return Value{BitVectorValue{maskValue(value, width), width}};
  }

  ValueKind kind() const {
    if (std::holds_alternative<int64_t>(data)) {
      return ValueKind::Int;
    }
    if (std::holds_alternative<bool>(data)) {
      return ValueKind::Bool;
    }
    return ValueKind::BitVec;
  }

  int64_t asInt() const { return std::get<int64_t>(data); }

  bool asBool() const { return std::get<bool>(data); }

  BitVectorValue asBitVector() const { return std::get<BitVectorValue>(data); }

  std::string toString() const {
    switch (kind()) {
      case ValueKind::Int:
        return std::to_string(asInt());
      case ValueKind::Bool:
        return asBool() ? "true" : "false";
      case ValueKind::BitVec:
        return asBitVector().toString();
    }
    return "unknown";
  }

 private:
  explicit Value(int64_t value) : data(value) {}
  explicit Value(bool value) : data(value) {}
  explicit Value(BitVectorValue value) : data(std::move(value)) {}

  static uint64_t maskValue(uint64_t value, uint32_t width) {
    if (width == 0) {
      return 0;
    }
    if (width >= 64) {
      return value;
    }
    return value & ((uint64_t{1} << width) - 1);
  }
};

using ValueEnvironment = std::map<std::string, Value>;
using DefineFunMap = std::unordered_map<std::string, const DefineFun*>;

struct EvaluationContext {
  const DefineFunMap* define_funs = nullptr;
  const SynthFun* synth_fun = nullptr;
  const SExpr* synth_candidate = nullptr;
};

struct SamplePool {
  std::vector<int64_t> ints;
  std::unordered_map<uint32_t, std::vector<uint64_t>> bit_vectors;
};

uint64_t maskBitVector(uint64_t value, uint32_t width) {
  if (width == 0) {
    return 0;
  }
  if (width >= 64) {
    return value;
  }
  return value & ((uint64_t{1} << width) - 1);
}

bool tryParseSignedInteger(std::string_view text, int64_t& output) {
  if (text.empty()) {
    return false;
  }

  size_t index = 0;
  if (text.front() == '-') {
    if (text.size() == 1) {
      return false;
    }
    index = 1;
  }

  for (; index < text.size(); ++index) {
    if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
      return false;
    }
  }

  try {
    output = std::stoll(std::string(text));
    return true;
  } catch (...) {
    return false;
  }
}

bool isBitVectorLiteralAtom(std::string_view atom) {
  return atom.starts_with("#x") || atom.starts_with("#b");
}

std::optional<BitVectorValue> parseBitVectorLiteral(std::string_view atom) {
  if (!isBitVectorLiteralAtom(atom)) {
    return std::nullopt;
  }

  const int base = atom[1] == 'x' ? 16 : 2;
  const uint32_t width = atom[1] == 'x'
                             ? static_cast<uint32_t>((atom.size() - 2) * 4)
                             : static_cast<uint32_t>(atom.size() - 2);

  try {
    const uint64_t value =
        std::stoull(std::string(atom.substr(2)), nullptr, base);
    return BitVectorValue{maskBitVector(value, width), width};
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<BitVectorValue> parseBitVectorLiteral(const SExpr& expr) {
  if (expr.isAtom()) {
    return parseBitVectorLiteral(expr.asAtom());
  }

  const auto& items = expr.asList();
  if (items.size() == 3 && items[0].isAtom() && items[0].asAtom() == "_" &&
      items[1].isAtom() && items[2].isAtom() &&
      items[1].asAtom().starts_with("bv")) {
    int64_t width = 0;
    if (!tryParseSignedInteger(items[2].asAtom(), width) || width <= 0) {
      return std::nullopt;
    }
    int64_t value = 0;
    if (!tryParseSignedInteger(items[1].asAtom().substr(2), value) ||
        value < 0) {
      return std::nullopt;
    }
    return BitVectorValue{maskBitVector(static_cast<uint64_t>(value),
                                        static_cast<uint32_t>(width)),
                          static_cast<uint32_t>(width)};
  }

  return std::nullopt;
}

std::optional<SortDescriptor> parseSort(const SExpr& expr) {
  if (expr.isAtom()) {
    if (expr.asAtom() == "Int") {
      return SortDescriptor{ValueKind::Int, 0};
    }
    if (expr.asAtom() == "Bool") {
      return SortDescriptor{ValueKind::Bool, 0};
    }
    return std::nullopt;
  }

  const auto& items = expr.asList();
  if (items.size() == 2 && items[0].isAtom() && items[0].asAtom() == "BitVec" &&
      items[1].isAtom()) {
    int64_t width = 0;
    if (!tryParseSignedInteger(items[1].asAtom(), width) || width <= 0) {
      return std::nullopt;
    }
    return SortDescriptor{ValueKind::BitVec, static_cast<uint32_t>(width)};
  }
  if (items.size() == 3 && items[0].isAtom() && items[0].asAtom() == "_" &&
      items[1].isAtom() && items[1].asAtom() == "BitVec" && items[2].isAtom()) {
    int64_t width = 0;
    if (!tryParseSignedInteger(items[2].asAtom(), width) || width <= 0) {
      return std::nullopt;
    }
    return SortDescriptor{ValueKind::BitVec, static_cast<uint32_t>(width)};
  }

  return std::nullopt;
}

bool valuesEqual(const Value& left, const Value& right) {
  if (left.kind() != right.kind()) {
    return false;
  }

  switch (left.kind()) {
    case ValueKind::Int:
      return left.asInt() == right.asInt();
    case ValueKind::Bool:
      return left.asBool() == right.asBool();
    case ValueKind::BitVec: {
      const BitVectorValue lhs = left.asBitVector();
      const BitVectorValue rhs = right.asBitVector();
      return lhs.width == rhs.width && lhs.value == rhs.value;
    }
  }

  return false;
}

bool isNonTerminalAtom(
    const std::string& atom,
    const std::unordered_map<std::string, GrammarRule>& grammar_by_name) {
  return grammar_by_name.find(atom) != grammar_by_name.end();
}

const DefineFun* findDefineFun(const DefineFunMap& define_funs,
                               const std::string& name) {
  const auto found = define_funs.find(name);
  if (found == define_funs.end()) {
    return nullptr;
  }
  return found->second;
}

Value evaluateExpression(const SExpr& expr, const ValueEnvironment& env,
                         const EvaluationContext& context);

Value applyBuiltin(const std::string& op, const std::vector<Value>& args) {
  if (op == "true" && args.empty()) {
    return Value::makeBool(true);
  }
  if (op == "false" && args.empty()) {
    return Value::makeBool(false);
  }

  if (op == "not") {
    if (args.size() != 1 || args[0].kind() != ValueKind::Bool) {
      throw std::runtime_error("'not' expects one Bool argument");
    }
    return Value::makeBool(!args[0].asBool());
  }

  if (op == "and" || op == "or") {
    bool accumulator = (op == "and");
    for (const auto& arg : args) {
      if (arg.kind() != ValueKind::Bool) {
        throw std::runtime_error("'" + op + "' expects Bool arguments");
      }
      if (op == "and") {
        accumulator = accumulator && arg.asBool();
      } else {
        accumulator = accumulator || arg.asBool();
      }
    }
    return Value::makeBool(accumulator);
  }

  if (op == "=>") {
    if (args.size() != 2 || args[0].kind() != ValueKind::Bool ||
        args[1].kind() != ValueKind::Bool) {
      throw std::runtime_error("'=>' expects two Bool arguments");
    }
    return Value::makeBool(!args[0].asBool() || args[1].asBool());
  }

  if (op == "=" || op == "distinct") {
    if (args.size() < 2) {
      throw std::runtime_error("'" + op + "' expects at least two arguments");
    }
    bool equal = true;
    for (size_t index = 1; index < args.size(); ++index) {
      equal = equal && valuesEqual(args[index - 1], args[index]);
    }
    return Value::makeBool(op == "=" ? equal : !equal);
  }

  if (op == "ite") {
    if (args.size() != 3 || args[0].kind() != ValueKind::Bool) {
      throw std::runtime_error("'ite' expects Bool, T, T");
    }
    if (args[1].kind() != args[2].kind()) {
      throw std::runtime_error("'ite' branch sorts must match");
    }
    return args[0].asBool() ? args[1] : args[2];
  }

  if (op == "+" || op == "-" || op == "*") {
    if (args.empty()) {
      throw std::runtime_error("'" + op + "' expects arguments");
    }
    for (const auto& arg : args) {
      if (arg.kind() != ValueKind::Int) {
        throw std::runtime_error("'" + op + "' expects Int arguments");
      }
    }

    if (op == "+") {
      int64_t sum = 0;
      for (const auto& arg : args) {
        sum += arg.asInt();
      }
      return Value::makeInt(sum);
    }

    if (op == "*") {
      int64_t product = 1;
      for (const auto& arg : args) {
        product *= arg.asInt();
      }
      return Value::makeInt(product);
    }

    if (args.size() == 1) {
      return Value::makeInt(-args[0].asInt());
    }

    int64_t difference = args[0].asInt();
    for (size_t index = 1; index < args.size(); ++index) {
      difference -= args[index].asInt();
    }
    return Value::makeInt(difference);
  }

  if (op == "<" || op == "<=" || op == ">" || op == ">=") {
    if (args.size() != 2 || args[0].kind() != ValueKind::Int ||
        args[1].kind() != ValueKind::Int) {
      throw std::runtime_error("'" + op + "' expects two Int arguments");
    }
    if (op == "<") {
      return Value::makeBool(args[0].asInt() < args[1].asInt());
    }
    if (op == "<=") {
      return Value::makeBool(args[0].asInt() <= args[1].asInt());
    }
    if (op == ">") {
      return Value::makeBool(args[0].asInt() > args[1].asInt());
    }
    return Value::makeBool(args[0].asInt() >= args[1].asInt());
  }

  if (op == "bvnot") {
    if (args.size() != 1 || args[0].kind() != ValueKind::BitVec) {
      throw std::runtime_error("'bvnot' expects one bit-vector argument");
    }
    const BitVectorValue bit_vector = args[0].asBitVector();
    return Value::makeBitVector(~bit_vector.value, bit_vector.width);
  }

  if (op == "bvadd" || op == "bvsub" || op == "bvmul" || op == "bvand" ||
      op == "bvor" || op == "bvxor") {
    if (args.size() != 2 || args[0].kind() != ValueKind::BitVec ||
        args[1].kind() != ValueKind::BitVec) {
      throw std::runtime_error("'" + op + "' expects two bit-vector arguments");
    }

    BitVectorValue left = args[0].asBitVector();
    BitVectorValue right = args[1].asBitVector();
    if (left.width != right.width) {
      throw std::runtime_error("bit-vector widths must match for '" + op + "'");
    }

    uint64_t value = 0;
    if (op == "bvadd") {
      value = left.value + right.value;
    } else if (op == "bvsub") {
      value = left.value - right.value;
    } else if (op == "bvmul") {
      value = left.value * right.value;
    } else if (op == "bvand") {
      value = left.value & right.value;
    } else if (op == "bvor") {
      value = left.value | right.value;
    } else {
      value = left.value ^ right.value;
    }
    return Value::makeBitVector(value, left.width);
  }

  throw std::runtime_error("Unsupported operator: " + op);
}

Value evaluateFunctionBody(const SExpr& body,
                           const std::vector<TypedIdentifier>& params,
                           const std::vector<SExpr>& arg_exprs,
                           const ValueEnvironment& env,
                           const EvaluationContext& context) {
  if (params.size() != arg_exprs.size()) {
    throw std::runtime_error("Function argument arity mismatch");
  }

  ValueEnvironment local_env = env;
  for (size_t index = 0; index < params.size(); ++index) {
    local_env[params[index].name] =
        evaluateExpression(arg_exprs[index], env, context);
  }
  return evaluateExpression(body, local_env, context);
}

Value evaluateExpression(const SExpr& expr, const ValueEnvironment& env,
                         const EvaluationContext& context) {
  if (expr.isAtom()) {
    const std::string& atom = expr.asAtom();
    if (const auto found = env.find(atom); found != env.end()) {
      return found->second;
    }

    if (atom == "true") {
      return Value::makeBool(true);
    }
    if (atom == "false") {
      return Value::makeBool(false);
    }

    if (const auto bit_vector = parseBitVectorLiteral(atom)) {
      return Value::makeBitVector(bit_vector->value, bit_vector->width);
    }

    int64_t integer = 0;
    if (tryParseSignedInteger(atom, integer)) {
      return Value::makeInt(integer);
    }

    throw std::runtime_error("Unknown symbol in evaluation: " + atom);
  }

  if (const auto bit_vector = parseBitVectorLiteral(expr)) {
    return Value::makeBitVector(bit_vector->value, bit_vector->width);
  }

  const auto& items = expr.asList();
  if (items.empty() || !items[0].isAtom()) {
    throw std::runtime_error("Malformed S-expression term: " + expr.toString());
  }

  const std::string& head = items[0].asAtom();
  std::vector<SExpr> arg_exprs(items.begin() + 1, items.end());

  if (context.synth_fun != nullptr && context.synth_candidate != nullptr &&
      head == context.synth_fun->name) {
    return evaluateFunctionBody(*context.synth_candidate,
                                context.synth_fun->params, arg_exprs, env,
                                context);
  }

  if (context.define_funs != nullptr) {
    if (const DefineFun* define_fun =
            findDefineFun(*context.define_funs, head)) {
      return evaluateFunctionBody(define_fun->body, define_fun->params,
                                  arg_exprs, env, context);
    }
  }

  std::vector<Value> args;
  args.reserve(arg_exprs.size());
  for (const auto& arg_expr : arg_exprs) {
    args.push_back(evaluateExpression(arg_expr, env, context));
  }
  return applyBuiltin(head, args);
}

std::string renderDefineFun(const SynthFun& synth_fun, const SExpr& body) {
  std::ostringstream output;
  output << "(define-fun " << synth_fun.name << " (";
  for (size_t index = 0; index < synth_fun.params.size(); ++index) {
    if (index != 0) {
      output << " ";
    }
    output << "(" << synth_fun.params[index].name << " "
           << synth_fun.params[index].sort.toString() << ")";
  }
  output << ") " << synth_fun.return_sort.toString() << " " << body.toString()
         << ")";
  return output.str();
}

void addUnique(std::vector<int64_t>& values, int64_t value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

void addUnique(std::vector<uint64_t>& values, uint64_t value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

void collectLiteralSamples(const SExpr& expr, SamplePool& pool) {
  if (expr.isAtom()) {
    int64_t integer = 0;
    if (tryParseSignedInteger(expr.asAtom(), integer)) {
      addUnique(pool.ints, integer);
      return;
    }
    if (const auto bit_vector = parseBitVectorLiteral(expr.asAtom())) {
      addUnique(pool.bit_vectors[bit_vector->width], bit_vector->value);
      return;
    }
    return;
  }

  if (const auto bit_vector = parseBitVectorLiteral(expr)) {
    addUnique(pool.bit_vectors[bit_vector->width], bit_vector->value);
    return;
  }

  for (const auto& child : expr.asList()) {
    collectLiteralSamples(child, pool);
  }
}

SamplePool buildSamplePool(const SyGuSProgram& program) {
  SamplePool pool;
  pool.ints = {-2, -1, 0, 1, 2};

  for (const auto& form : program.top_level_forms) {
    collectLiteralSamples(form, pool);
  }

  for (auto& [width, values] : pool.bit_vectors) {
    addUnique(values, 0);
    addUnique(values, 1);
    addUnique(values, 2);
    addUnique(values, 3);
    addUnique(values, maskBitVector(~uint64_t{0}, width));
  }

  return pool;
}

std::vector<Value> domainForSort(const SortDescriptor& sort,
                                 const SamplePool& pool) {
  std::vector<Value> values;
  switch (sort.kind) {
    case ValueKind::Int:
      values.reserve(pool.ints.size());
      for (const int64_t value : pool.ints) {
        values.push_back(Value::makeInt(value));
      }
      break;
    case ValueKind::Bool:
      values.push_back(Value::makeBool(false));
      values.push_back(Value::makeBool(true));
      break;
    case ValueKind::BitVec: {
      auto found = pool.bit_vectors.find(sort.bit_width);
      std::vector<uint64_t> bit_values;
      if (found != pool.bit_vectors.end()) {
        bit_values = found->second;
      }
      if (bit_values.empty()) {
        bit_values = {0, 1, 2, 3, maskBitVector(~uint64_t{0}, sort.bit_width)};
      }
      values.reserve(bit_values.size());
      for (const uint64_t value : bit_values) {
        values.push_back(Value::makeBitVector(value, sort.bit_width));
      }
      break;
    }
  }
  return values;
}

std::vector<ValueEnvironment> buildAssignments(
    const std::vector<TypedIdentifier>& vars, const SamplePool& pool,
    size_t max_assignments) {
  std::vector<ValueEnvironment> assignments;
  if (vars.empty()) {
    assignments.push_back({});
    return assignments;
  }

  std::vector<std::vector<Value>> domains;
  domains.reserve(vars.size());
  for (const auto& var : vars) {
    const auto sort = parseSort(var.sort);
    if (!sort.has_value()) {
      return assignments;
    }
    domains.push_back(domainForSort(*sort, pool));
  }

  ValueEnvironment current;
  std::function<void(size_t)> expand = [&](size_t index) {
    if (assignments.size() >= max_assignments) {
      return;
    }
    if (index == vars.size()) {
      assignments.push_back(current);
      return;
    }

    for (const auto& value : domains[index]) {
      current[vars[index].name] = value;
      expand(index + 1);
      if (assignments.size() >= max_assignments) {
        return;
      }
    }
  };

  expand(0);
  return assignments;
}

std::vector<TypedIdentifier> asTypedIdentifiers(
    const std::vector<DeclareVar>& vars) {
  std::vector<TypedIdentifier> identifiers;
  identifiers.reserve(vars.size());
  for (const auto& var : vars) {
    identifiers.push_back(TypedIdentifier{var.name, var.sort, var.type});
  }
  return identifiers;
}

bool satisfiesConstraintsOnSamples(const SyGuSProgram& program,
                                   const SynthFun& synth_fun,
                                   const SExpr& candidate,
                                   const std::vector<ValueEnvironment>& samples,
                                   std::string& error) {
  DefineFunMap define_funs;
  for (const auto& define_fun : program.define_funs) {
    define_funs.emplace(define_fun.name, &define_fun);
  }

  EvaluationContext context{&define_funs, &synth_fun, &candidate};
  for (const auto& sample : samples) {
    for (const auto& constraint : program.constraints) {
      try {
        const Value result =
            evaluateExpression(constraint.expression, sample, context);
        if (result.kind() != ValueKind::Bool || !result.asBool()) {
          return false;
        }
      } catch (const std::exception& exception) {
        error = exception.what();
        return false;
      }
    }
  }

  return true;
}

std::string semanticSignature(const SExpr& expr,
                              const std::vector<ValueEnvironment>& samples,
                              std::string& error) {
  EvaluationContext context;
  std::ostringstream output;
  for (const auto& sample : samples) {
    try {
      output << evaluateExpression(expr, sample, context).toString() << ";";
    } catch (const std::exception& exception) {
      error = exception.what();
      return {};
    }
  }
  return output.str();
}

bool isFixedTerminal(
    const SExpr& expr,
    const std::unordered_map<std::string, GrammarRule>& grammar_by_name) {
  if (expr.isAtom()) {
    return !isNonTerminalAtom(expr.asAtom(), grammar_by_name);
  }
  return parseBitVectorLiteral(expr).has_value();
}

std::vector<SExpr> generateExpressionsForSize(
    const GrammarRule& rule, size_t target_size,
    const std::unordered_map<std::string, GrammarRule>& grammar_by_name,
    const std::unordered_map<std::string,
                             std::unordered_map<size_t, std::vector<SExpr>>>&
        by_symbol_and_size) {
  std::vector<SExpr> generated;

  std::function<std::vector<SExpr>(const SExpr&, size_t)> expandTemplate =
      [&](const SExpr& expr, size_t size) -> std::vector<SExpr> {
    if (expr.isAtom()) {
      if (isNonTerminalAtom(expr.asAtom(), grammar_by_name)) {
        const auto symbol_it = by_symbol_and_size.find(expr.asAtom());
        if (symbol_it == by_symbol_and_size.end()) {
          return {};
        }
        const auto size_it = symbol_it->second.find(size);
        if (size_it == symbol_it->second.end()) {
          return {};
        }
        return size_it->second;
      }
      return size == 1 ? std::vector<SExpr>{expr} : std::vector<SExpr>{};
    }

    if (parseBitVectorLiteral(expr).has_value()) {
      return size == 1 ? std::vector<SExpr>{expr} : std::vector<SExpr>{};
    }

    const auto& items = expr.asList();
    if (items.empty() || !items[0].isAtom() || size < 2) {
      return {};
    }

    const std::string head = items[0].asAtom();
    const size_t child_count = items.size() - 1;
    const size_t root_cost = 1;
    if (size <= root_cost) {
      return {};
    }

    std::vector<bool> expandable(child_count, false);
    size_t fixed_cost = 0;
    for (size_t index = 0; index < child_count; ++index) {
      expandable[index] = !isFixedTerminal(items[index + 1], grammar_by_name);
      if (!expandable[index]) {
        ++fixed_cost;
      }
    }

    if (size < root_cost + fixed_cost) {
      return {};
    }

    const size_t variable_count =
        std::count(expandable.begin(), expandable.end(), true);
    if (size < root_cost + fixed_cost + variable_count) {
      return {};
    }

    const size_t extra_budget = size - root_cost - fixed_cost - variable_count;
    if (variable_count == 0) {
      if (size != root_cost + fixed_cost) {
        return {};
      }
      return {expr};
    }

    std::vector<size_t> child_sizes(child_count, 1);
    std::vector<SExpr> results;

    std::function<void(size_t, size_t)> assignSizes =
        [&](size_t index, size_t remaining_budget) {
          if (index == child_count) {
            if (remaining_budget != 0) {
              return;
            }

            std::vector<std::vector<SExpr>> child_options(child_count);
            for (size_t child_index = 0; child_index < child_count;
                 ++child_index) {
              child_options[child_index] = expandTemplate(
                  items[child_index + 1], child_sizes[child_index]);
              if (child_options[child_index].empty()) {
                return;
              }
            }

            std::vector<SExpr> built_children(child_count);
            std::function<void(size_t)> buildProducts =
                [&](size_t child_index) {
                  if (child_index == child_count) {
                    std::vector<SExpr> rebuilt;
                    rebuilt.reserve(items.size());
                    rebuilt.push_back(SExpr::makeAtom(head));
                    rebuilt.insert(rebuilt.end(), built_children.begin(),
                                   built_children.end());
                    results.push_back(SExpr::makeList(std::move(rebuilt)));
                    return;
                  }

                  for (const auto& option : child_options[child_index]) {
                    built_children[child_index] = option;
                    buildProducts(child_index + 1);
                  }
                };

            buildProducts(0);
            return;
          }

          if (!expandable[index]) {
            child_sizes[index] = 1;
            assignSizes(index + 1, remaining_budget);
            return;
          }

          for (size_t child_size = 1; child_size <= remaining_budget + 1;
               ++child_size) {
            child_sizes[index] = child_size;
            assignSizes(index + 1, remaining_budget - (child_size - 1));
          }
        };

    assignSizes(0, extra_budget);
    return results;
  };

  for (const auto& production : rule.productions) {
    auto produced = expandTemplate(production, target_size);
    generated.insert(generated.end(), produced.begin(), produced.end());
  }

  return generated;
}

#if SYGUS_HAVE_CVC5
using TermEnvironment = std::map<std::string, cvc5::Term>;

cvc5::Sort translateSort(cvc5::Solver& solver, const SortDescriptor& sort) {
  switch (sort.kind) {
    case ValueKind::Int:
      return solver.getIntegerSort();
    case ValueKind::Bool:
      return solver.getBooleanSort();
    case ValueKind::BitVec:
      return solver.mkBitVectorSort(sort.bit_width);
  }
  throw std::runtime_error("Unsupported sort");
}

cvc5::Term translateExpression(cvc5::Solver& solver, const SExpr& expr,
                               const TermEnvironment& env,
                               const DefineFunMap& define_funs,
                               const SynthFun* synth_fun,
                               const SExpr* synth_candidate);

cvc5::Term translateFunctionBody(cvc5::Solver& solver, const SExpr& body,
                                 const std::vector<TypedIdentifier>& params,
                                 const std::vector<SExpr>& arg_exprs,
                                 const TermEnvironment& env,
                                 const DefineFunMap& define_funs,
                                 const SynthFun* synth_fun,
                                 const SExpr* synth_candidate) {
  if (params.size() != arg_exprs.size()) {
    throw std::runtime_error("Function argument arity mismatch");
  }

  TermEnvironment local_env = env;
  for (size_t index = 0; index < params.size(); ++index) {
    local_env[params[index].name] = translateExpression(
        solver, arg_exprs[index], env, define_funs, synth_fun, synth_candidate);
  }
  return translateExpression(solver, body, local_env, define_funs, synth_fun,
                             synth_candidate);
}

cvc5::Term mkNaryTerm(cvc5::Solver& solver, cvc5::Kind kind,
                      const std::vector<cvc5::Term>& args) {
  if (args.empty()) {
    throw std::runtime_error("Cannot build n-ary term with zero arguments");
  }
  if (args.size() == 1) {
    return args.front();
  }
  return solver.mkTerm(kind, args);
}

cvc5::Term translateBuiltin(cvc5::Solver& solver, const std::string& op,
                            const std::vector<cvc5::Term>& args) {
  if (op == "and") {
    return mkNaryTerm(solver, cvc5::Kind::AND, args);
  }
  if (op == "or") {
    return mkNaryTerm(solver, cvc5::Kind::OR, args);
  }
  if (op == "not") {
    return solver.mkTerm(cvc5::Kind::NOT, {args.at(0)});
  }
  if (op == "=>") {
    return solver.mkTerm(cvc5::Kind::IMPLIES, {args.at(0), args.at(1)});
  }
  if (op == "=") {
    return solver.mkTerm(cvc5::Kind::EQUAL, {args.at(0), args.at(1)});
  }
  if (op == "distinct") {
    return solver.mkTerm(cvc5::Kind::DISTINCT, args);
  }
  if (op == "ite") {
    return solver.mkTerm(cvc5::Kind::ITE, {args.at(0), args.at(1), args.at(2)});
  }
  if (op == "+") {
    return mkNaryTerm(solver, cvc5::Kind::ADD, args);
  }
  if (op == "*") {
    return mkNaryTerm(solver, cvc5::Kind::MULT, args);
  }
  if (op == "-") {
    if (args.size() == 1) {
      return solver.mkTerm(cvc5::Kind::SUB,
                           {solver.mkInteger("0"), args.front()});
    }
    return mkNaryTerm(solver, cvc5::Kind::SUB, args);
  }
  if (op == "<") {
    return solver.mkTerm(cvc5::Kind::LT, {args.at(0), args.at(1)});
  }
  if (op == "<=") {
    return solver.mkTerm(cvc5::Kind::LEQ, {args.at(0), args.at(1)});
  }
  if (op == ">") {
    return solver.mkTerm(cvc5::Kind::GT, {args.at(0), args.at(1)});
  }
  if (op == ">=") {
    return solver.mkTerm(cvc5::Kind::GEQ, {args.at(0), args.at(1)});
  }
  if (op == "bvadd") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_ADD, {args.at(0), args.at(1)});
  }
  if (op == "bvsub") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_SUB, {args.at(0), args.at(1)});
  }
  if (op == "bvmul") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_MULT, {args.at(0), args.at(1)});
  }
  if (op == "bvand") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_AND, {args.at(0), args.at(1)});
  }
  if (op == "bvor") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_OR, {args.at(0), args.at(1)});
  }
  if (op == "bvxor") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_XOR, {args.at(0), args.at(1)});
  }
  if (op == "bvnot") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_NOT, {args.at(0)});
  }

  throw std::runtime_error("Unsupported operator for cvc5 translation: " + op);
}

cvc5::Term translateExpression(cvc5::Solver& solver, const SExpr& expr,
                               const TermEnvironment& env,
                               const DefineFunMap& define_funs,
                               const SynthFun* synth_fun,
                               const SExpr* synth_candidate) {
  if (expr.isAtom()) {
    const std::string& atom = expr.asAtom();
    if (const auto found = env.find(atom); found != env.end()) {
      return found->second;
    }
    if (atom == "true" || atom == "false") {
      return solver.mkBoolean(atom == "true");
    }
    if (const auto bit_vector = parseBitVectorLiteral(atom)) {
      return solver.mkBitVector(bit_vector->width, bit_vector->value);
    }
    int64_t integer = 0;
    if (tryParseSignedInteger(atom, integer)) {
      return solver.mkInteger(atom);
    }
    throw std::runtime_error("Unknown symbol for cvc5 translation: " + atom);
  }

  if (const auto bit_vector = parseBitVectorLiteral(expr)) {
    return solver.mkBitVector(bit_vector->width, bit_vector->value);
  }

  const auto& items = expr.asList();
  if (items.empty() || !items[0].isAtom()) {
    throw std::runtime_error("Malformed expression for cvc5 translation");
  }

  const std::string& head = items[0].asAtom();
  std::vector<SExpr> arg_exprs(items.begin() + 1, items.end());

  if (synth_fun != nullptr && synth_candidate != nullptr &&
      head == synth_fun->name) {
    return translateFunctionBody(solver, *synth_candidate, synth_fun->params,
                                 arg_exprs, env, define_funs, synth_fun,
                                 synth_candidate);
  }

  if (const DefineFun* define_fun = findDefineFun(define_funs, head)) {
    return translateFunctionBody(solver, define_fun->body, define_fun->params,
                                 arg_exprs, env, define_funs, synth_fun,
                                 synth_candidate);
  }

  std::vector<cvc5::Term> args;
  args.reserve(arg_exprs.size());
  for (const auto& arg_expr : arg_exprs) {
    args.push_back(translateExpression(solver, arg_expr, env, define_funs,
                                       synth_fun, synth_candidate));
  }
  return translateBuiltin(solver, head, args);
}

bool verifyWithCvc5(const SyGuSProgram& program, const SynthFun& synth_fun,
                    const SExpr& candidate, std::string& message) {
  cvc5::Solver solver;
  solver.setLogic(program.logic.empty() ? "ALL" : program.logic);

  DefineFunMap define_funs;
  for (const auto& define_fun : program.define_funs) {
    define_funs.emplace(define_fun.name, &define_fun);
  }

  TermEnvironment env;
  for (const auto& variable : program.declare_vars) {
    const auto sort = parseSort(variable.sort);
    if (!sort.has_value()) {
      message = "Unsupported declared variable sort: " + variable.type;
      return false;
    }
    env.emplace(variable.name,
                solver.mkConst(translateSort(solver, *sort), variable.name));
  }

  std::vector<cvc5::Term> constraints;
  constraints.reserve(program.constraints.size());
  try {
    for (const auto& constraint : program.constraints) {
      constraints.push_back(translateExpression(solver, constraint.expression,
                                                env, define_funs, &synth_fun,
                                                &candidate));
    }
  } catch (const std::exception& exception) {
    message = exception.what();
    return false;
  }

  cvc5::Term conjunction = solver.mkBoolean(true);
  if (!constraints.empty()) {
    conjunction = constraints.size() == 1
                      ? constraints.front()
                      : solver.mkTerm(cvc5::Kind::AND, constraints);
  }
  solver.assertFormula(solver.mkTerm(cvc5::Kind::NOT, {conjunction}));

  const cvc5::Result result = solver.checkSat();
  if (result.isUnsat()) {
    return true;
  }
  if (result.isSat()) {
    message = "cvc5 found a counterexample for the candidate";
    return false;
  }

  message = "cvc5 returned unknown while verifying the candidate";
  return false;
}
#endif

}  // namespace

bool SyGuSSolver::hasCvc5() {
#if SYGUS_HAVE_CVC5
  return true;
#else
  return false;
#endif
}

std::string SyGuSSolver::statusToString(SolveResult::Status status) {
  switch (status) {
    case SolveResult::Status::Solved:
      return "solved";
    case SolveResult::Status::Unsupported:
      return "unsupported";
    case SolveResult::Status::Exhausted:
      return "exhausted";
    case SolveResult::Status::Error:
      return "error";
  }
  return "error";
}

SolveResult SyGuSSolver::solve(const SyGuSProgram& program,
                               const SolveOptions& options) const {
  SolveResult result;
  result.logic = program.logic;
  result.cvc5_available = hasCvc5();

  if (!program.synth_invs.empty() || !program.inv_constraints.empty()) {
    result.status = SolveResult::Status::Unsupported;
    result.message = "Invariant synthesis is not supported yet.";
    return result;
  }
  if (program.synth_funs.empty()) {
    result.status = SolveResult::Status::Unsupported;
    result.message = "No synth-fun command was found.";
    return result;
  }
  if (program.synth_funs.size() != 1) {
    result.status = SolveResult::Status::Unsupported;
    result.message = "Only single synth-fun problems are supported.";
    return result;
  }
  if (!program.declare_primed_vars.empty()) {
    result.status = SolveResult::Status::Unsupported;
    result.message = "Primed variables are currently unsupported.";
    return result;
  }

  const SynthFun& synth_fun = program.synth_funs.front();
  if (synth_fun.grammar_rules.empty()) {
    result.status = SolveResult::Status::Unsupported;
    result.message = "The synth-fun grammar is empty.";
    return result;
  }

  const auto return_sort = parseSort(synth_fun.return_sort);
  if (!return_sort.has_value()) {
    result.status = SolveResult::Status::Unsupported;
    result.message =
        "Unsupported synth-fun return sort: " + synth_fun.return_type;
    return result;
  }

  if (return_sort->kind != ValueKind::Int &&
      return_sort->kind != ValueKind::BitVec) {
    result.status = SolveResult::Status::Unsupported;
    result.message =
        "Only Int and BitVec synth-fun return sorts are supported right now.";
    return result;
  }

  std::unordered_map<std::string, GrammarRule> grammar_by_name;
  for (const auto& rule : synth_fun.grammar_rules) {
    grammar_by_name.emplace(rule.non_terminal, rule);
  }

  const std::string start_symbol = synth_fun.grammar_rules.front().non_terminal;
  const SamplePool sample_pool = buildSamplePool(program);
  std::vector<ValueEnvironment> param_samples =
      buildAssignments(synth_fun.params, sample_pool,
                       std::max<size_t>(1, options.max_sample_assignments));

  std::vector<ValueEnvironment> constraint_samples =
      buildAssignments(asTypedIdentifiers(program.declare_vars), sample_pool,
                       std::max<size_t>(1, options.max_sample_assignments));

  if (param_samples.empty() || constraint_samples.empty()) {
    result.status = SolveResult::Status::Unsupported;
    result.message =
        "Unable to build sample assignments for the current benchmark.";
    return result;
  }

  std::unordered_map<std::string,
                     std::unordered_map<size_t, std::vector<SExpr>>>
      expressions_by_symbol_and_size;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      seen_signatures_by_symbol;

  for (size_t size = 1; size <= options.max_expression_size; ++size) {
    for (const auto& rule : synth_fun.grammar_rules) {
      auto generated = generateExpressionsForSize(
          rule, size, grammar_by_name, expressions_by_symbol_and_size);

      for (const auto& expr : generated) {
        std::string signature_error;
        const std::string signature =
            semanticSignature(expr, param_samples, signature_error);
        if (signature.empty()) {
          continue;
        }

        auto& seen_signatures = seen_signatures_by_symbol[rule.non_terminal];
        if (!seen_signatures.insert(signature).second) {
          continue;
        }

        expressions_by_symbol_and_size[rule.non_terminal][size].push_back(expr);

        if (rule.non_terminal != start_symbol) {
          continue;
        }

        ++result.enumerated_candidates;

        std::string sample_error;
        if (!satisfiesConstraintsOnSamples(program, synth_fun, expr,
                                           constraint_samples, sample_error)) {
          continue;
        }

        ++result.tested_candidates;
        bool exact_match = true;
        std::string verification_message;

        if (options.require_cvc5_verification) {
#if SYGUS_HAVE_CVC5
          exact_match =
              verifyWithCvc5(program, synth_fun, expr, verification_message);
          result.cvc5_verified = exact_match;
#else
          exact_match = true;
          verification_message =
              "cvc5 verification was requested but this build has no cvc5; "
              "the candidate is sample-validated only.";
#endif
        }

        if (exact_match || !options.require_cvc5_verification) {
          result.status = SolveResult::Status::Solved;
          result.solution = expr.toString();
          result.define_fun = renderDefineFun(synth_fun, expr);
          if (!options.require_cvc5_verification &&
              !verification_message.empty()) {
            result.message = verification_message;
          }
          return result;
        }

        if (!verification_message.empty()) {
          result.message = verification_message;
        }

        if (result.tested_candidates >= options.max_candidates) {
          result.status = SolveResult::Status::Exhausted;
          result.message =
              "Reached the verified-candidate budget before finding a "
              "solution.";
          return result;
        }
      }
    }
  }

  result.status = SolveResult::Status::Exhausted;
  if (result.message.empty()) {
    result.message =
        "No candidate satisfied the search budget for the current benchmark.";
  }
  return result;
}
