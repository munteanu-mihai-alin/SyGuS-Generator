#include "sygus_solver.hpp"

#include "candidate_predictor.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <random>
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
      op == "bvor" || op == "bvxor" || op == "bvudiv" || op == "bvurem" ||
      op == "bvshl" || op == "bvlshr" || op == "bvashr" || op == "bvsdiv" ||
      op == "bvsrem" || op == "bvsmod") {
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
    } else if (op == "bvxor") {
      value = left.value ^ right.value;
    } else if (op == "bvudiv") {
      value = right.value == 0 ? maskBitVector(~uint64_t{0}, left.width)
                               : left.value / right.value;
    } else if (op == "bvurem") {
      value = right.value == 0 ? left.value : left.value % right.value;
    } else if (op == "bvshl") {
      value = right.value >= left.width ? 0 : left.value << right.value;
    } else if (op == "bvlshr") {
      value = right.value >= left.width ? 0 : left.value >> right.value;
    } else if (op == "bvashr") {
      bool sign = (left.value >> (left.width - 1)) & 1;
      if (right.value >= left.width) {
        value = sign ? maskBitVector(~uint64_t{0}, left.width) : 0;
      } else {
        if (sign) {
          uint64_t fill = maskBitVector(~uint64_t{0}, left.width)
                          << (left.width - right.value);
          value = (left.value >> right.value) | fill;
        } else {
          value = left.value >> right.value;
        }
      }
    } else if (op == "bvsdiv") {
      if (right.value == 0) {
        value = maskBitVector(~uint64_t{0}, left.width);
      } else {
        int64_t sl = static_cast<int64_t>(left.value << (64 - left.width)) >>
                     (64 - left.width);
        int64_t sr = static_cast<int64_t>(right.value << (64 - left.width)) >>
                     (64 - left.width);
        value = static_cast<uint64_t>(sl / sr);
      }
    } else if (op == "bvsrem") {
      if (right.value == 0) {
        value = left.value;
      } else {
        int64_t sl = static_cast<int64_t>(left.value << (64 - left.width)) >>
                     (64 - left.width);
        int64_t sr = static_cast<int64_t>(right.value << (64 - left.width)) >>
                     (64 - left.width);
        value = static_cast<uint64_t>(sl % sr);
      }
    } else if (op == "bvsmod") {
      if (right.value == 0) {
        value = left.value;
      } else {
        int64_t sl = static_cast<int64_t>(left.value << (64 - left.width)) >>
                     (64 - left.width);
        int64_t sr = static_cast<int64_t>(right.value << (64 - left.width)) >>
                     (64 - left.width);
        int64_t rem = sl % sr;
        if (rem != 0 && ((rem ^ sr) < 0)) rem += sr;
        value = static_cast<uint64_t>(rem);
      }
    }
    return Value::makeBitVector(value, left.width);
  }

  if (op == "bvneg") {
    if (args.size() != 1 || args[0].kind() != ValueKind::BitVec) {
      throw std::runtime_error("'bvneg' expects one bit-vector argument");
    }
    const BitVectorValue bv = args[0].asBitVector();
    return Value::makeBitVector(~bv.value + 1, bv.width);
  }

  if (op == "bvult" || op == "bvule" || op == "bvugt" || op == "bvuge" ||
      op == "bvslt" || op == "bvsle" || op == "bvsgt" || op == "bvsge") {
    if (args.size() != 2 || args[0].kind() != ValueKind::BitVec ||
        args[1].kind() != ValueKind::BitVec) {
      throw std::runtime_error("'" + op + "' expects two bit-vector arguments");
    }
    BitVectorValue left = args[0].asBitVector();
    BitVectorValue right = args[1].asBitVector();
    if (left.width != right.width) {
      throw std::runtime_error("bit-vector widths must match for '" + op + "'");
    }
    if (op == "bvult") return Value::makeBool(left.value < right.value);
    if (op == "bvule") return Value::makeBool(left.value <= right.value);
    if (op == "bvugt") return Value::makeBool(left.value > right.value);
    if (op == "bvuge") return Value::makeBool(left.value >= right.value);
    int64_t sl = static_cast<int64_t>(left.value << (64 - left.width)) >>
                 (64 - left.width);
    int64_t sr = static_cast<int64_t>(right.value << (64 - left.width)) >>
                 (64 - left.width);
    if (op == "bvslt") return Value::makeBool(sl < sr);
    if (op == "bvsle") return Value::makeBool(sl <= sr);
    if (op == "bvsgt") return Value::makeBool(sl > sr);
    return Value::makeBool(sl >= sr);
  }

  if (op == "div" || op == "mod" || op == "abs") {
    if (op == "abs") {
      if (args.size() != 1 || args[0].kind() != ValueKind::Int) {
        throw std::runtime_error("'abs' expects one Int argument");
      }
      int64_t v = args[0].asInt();
      return Value::makeInt(v < 0 ? -v : v);
    }
    if (args.size() != 2 || args[0].kind() != ValueKind::Int ||
        args[1].kind() != ValueKind::Int) {
      throw std::runtime_error("'" + op + "' expects two Int arguments");
    }
    int64_t a = args[0].asInt();
    int64_t b = args[1].asInt();
    if (b == 0) {
      throw std::runtime_error("Division by zero in '" + op + "'");
    }
    if (op == "div") {
      int64_t q = a / b;
      if ((a % b != 0) && ((a ^ b) < 0)) --q;
      return Value::makeInt(q);
    }
    int64_t q = a / b;
    if ((a % b != 0) && ((a ^ b) < 0)) --q;
    return Value::makeInt(a - q * b);
  }

  if (op == "xor") {
    if (args.size() != 2 || args[0].kind() != ValueKind::Bool ||
        args[1].kind() != ValueKind::Bool) {
      throw std::runtime_error("'xor' expects two Bool arguments");
    }
    return Value::makeBool(args[0].asBool() != args[1].asBool());
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

  if (head == "let") {
    if (items.size() != 3) {
      throw std::runtime_error("'let' expects bindings and a body");
    }
    const auto& bindings = items[1].asList();
    ValueEnvironment local_env = env;
    for (const auto& binding : bindings) {
      const auto& bparts = binding.asList();
      if (bparts.size() != 2) {
        throw std::runtime_error("Invalid let binding");
      }
      local_env[bparts[0].asAtom()] =
          evaluateExpression(bparts[1], local_env, context);
    }
    return evaluateExpression(items[2], local_env, context);
  }

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
  if (op == "bvneg") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_NEG, {args.at(0)});
  }
  if (op == "bvudiv") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_UDIV, {args.at(0), args.at(1)});
  }
  if (op == "bvurem") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_UREM, {args.at(0), args.at(1)});
  }
  if (op == "bvshl") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_SHL, {args.at(0), args.at(1)});
  }
  if (op == "bvlshr") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_LSHR, {args.at(0), args.at(1)});
  }
  if (op == "bvashr") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_ASHR, {args.at(0), args.at(1)});
  }
  if (op == "bvsdiv") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_SDIV, {args.at(0), args.at(1)});
  }
  if (op == "bvsrem") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_SREM, {args.at(0), args.at(1)});
  }
  if (op == "bvsmod") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_SMOD, {args.at(0), args.at(1)});
  }
  if (op == "bvult") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_ULT, {args.at(0), args.at(1)});
  }
  if (op == "bvule") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_ULE, {args.at(0), args.at(1)});
  }
  if (op == "bvugt") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_UGT, {args.at(0), args.at(1)});
  }
  if (op == "bvuge") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_UGE, {args.at(0), args.at(1)});
  }
  if (op == "bvslt") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_SLT, {args.at(0), args.at(1)});
  }
  if (op == "bvsle") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_SLE, {args.at(0), args.at(1)});
  }
  if (op == "bvsgt") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_SGT, {args.at(0), args.at(1)});
  }
  if (op == "bvsge") {
    return solver.mkTerm(cvc5::Kind::BITVECTOR_SGE, {args.at(0), args.at(1)});
  }
  if (op == "div") {
    return solver.mkTerm(cvc5::Kind::INTS_DIVISION, {args.at(0), args.at(1)});
  }
  if (op == "mod") {
    return solver.mkTerm(cvc5::Kind::INTS_MODULUS, {args.at(0), args.at(1)});
  }
  if (op == "abs") {
    return solver.mkTerm(cvc5::Kind::ABS, {args.at(0)});
  }
  if (op == "xor") {
    return solver.mkTerm(cvc5::Kind::XOR, {args.at(0), args.at(1)});
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

  if (head == "let") {
    if (items.size() != 3) {
      throw std::runtime_error("'let' expects bindings and a body");
    }
    const auto& bindings = items[1].asList();
    TermEnvironment local_env = env;
    for (const auto& binding : bindings) {
      const auto& bparts = binding.asList();
      if (bparts.size() != 2) {
        throw std::runtime_error("Invalid let binding in cvc5 translation");
      }
      local_env[bparts[0].asAtom()] = translateExpression(
          solver, bparts[1], local_env, define_funs, synth_fun, synth_candidate);
    }
    return translateExpression(solver, items[2], local_env, define_funs,
                               synth_fun, synth_candidate);
  }

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

enum class VerifyStatus { Valid, Counterexample, Unknown, Error };

struct VerifyResult {
  VerifyStatus status = VerifyStatus::Error;
  std::string message;
  ValueEnvironment counterexample;
};

Value cvc5TermToValue(const cvc5::Term& term, const SortDescriptor& sort) {
  switch (sort.kind) {
    case ValueKind::Int:
      return Value::makeInt(
          static_cast<int64_t>(std::stoll(term.getIntegerValue())));
    case ValueKind::Bool:
      return Value::makeBool(term.getBooleanValue());
    case ValueKind::BitVec:
      return Value::makeBitVector(
          static_cast<uint64_t>(
              std::stoull(term.getBitVectorValue(), nullptr, 2)),
          sort.bit_width);
  }
  return Value::makeInt(0);
}

VerifyResult verifyWithCvc5(const SyGuSProgram& program,
                            const SynthFun& synth_fun, const SExpr& candidate) {
  VerifyResult verify;
  cvc5::Solver solver;
  solver.setLogic(program.logic.empty() ? "ALL" : program.logic);
  solver.setOption("produce-models", "true");

  DefineFunMap define_funs;
  for (const auto& define_fun : program.define_funs) {
    define_funs.emplace(define_fun.name, &define_fun);
  }

  TermEnvironment env;
  std::vector<std::pair<std::string, SortDescriptor>> var_sorts;
  for (const auto& variable : program.declare_vars) {
    const auto sort = parseSort(variable.sort);
    if (!sort.has_value()) {
      verify.status = VerifyStatus::Error;
      verify.message = "Unsupported declared variable sort: " + variable.type;
      return verify;
    }
    cvc5::Term var =
        solver.mkConst(translateSort(solver, *sort), variable.name);
    env.emplace(variable.name, var);
    var_sorts.emplace_back(variable.name, *sort);
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
    verify.status = VerifyStatus::Error;
    verify.message = exception.what();
    return verify;
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
    verify.status = VerifyStatus::Valid;
    return verify;
  }
  if (result.isSat()) {
    verify.status = VerifyStatus::Counterexample;
    for (const auto& [name, sort] : var_sorts) {
      try {
        cvc5::Term model_value = solver.getValue(env.at(name));
        verify.counterexample[name] = cvc5TermToValue(model_value, sort);
      } catch (...) {
      }
    }
    return verify;
  }

  verify.status = VerifyStatus::Unknown;
  verify.message = "cvc5 returned unknown while verifying the candidate";
  return verify;
}
#endif

size_t countNodes(const SExpr& expr) {
  if (expr.isAtom()) return 1;
  size_t count = 0;
  for (const auto& child : expr.asList()) {
    count += countNodes(child);
  }
  return count;
}

SExpr getSubtreeAt(const SExpr& expr, size_t target_index, size_t& current) {
  if (current == target_index) return expr;
  if (expr.isAtom()) { ++current; return expr; }
  ++current;
  for (const auto& child : expr.asList()) {
    if (current > target_index) break;
    auto result = getSubtreeAt(child, target_index, current);
    if (current > target_index) return result;
  }
  return expr;
}

SExpr replaceSubtreeAt(const SExpr& expr, size_t target_index,
                       const SExpr& replacement, size_t& current) {
  if (current == target_index) {
    ++current;
    return replacement;
  }
  if (expr.isAtom()) { ++current; return expr; }
  ++current;
  std::vector<SExpr> new_items;
  for (const auto& child : expr.asList()) {
    new_items.push_back(replaceSubtreeAt(child, target_index, replacement, current));
  }
  return SExpr::makeList(std::move(new_items));
}

SExpr crossover(const SExpr& parent1, const SExpr& parent2, std::mt19937& rng) {
  size_t n1 = countNodes(parent1);
  size_t n2 = countNodes(parent2);
  if (n1 <= 1 || n2 <= 1) return parent1;

  std::uniform_int_distribution<size_t> dist1(0, n1 - 1);
  std::uniform_int_distribution<size_t> dist2(0, n2 - 1);

  size_t pos2 = dist2(rng);
  size_t cursor = 0;
  SExpr subtree = getSubtreeAt(parent2, pos2, cursor);

  size_t pos1 = dist1(rng);
  cursor = 0;
  return replaceSubtreeAt(parent1, pos1, subtree, cursor);
}

SExpr mutateExpr(const SExpr& expr, const std::vector<SExpr>& gene_pool,
                 std::mt19937& rng) {
  size_t n = countNodes(expr);
  if (n == 0 || gene_pool.empty()) return expr;

  std::uniform_int_distribution<size_t> pos_dist(0, n - 1);
  std::uniform_int_distribution<size_t> pool_dist(0, gene_pool.size() - 1);

  const SExpr& donor = gene_pool[pool_dist(rng)];
  size_t donor_nodes = countNodes(donor);
  std::uniform_int_distribution<size_t> donor_dist(0, donor_nodes - 1);

  size_t donor_pos = donor_dist(rng);
  size_t cursor = 0;
  SExpr subtree = getSubtreeAt(donor, donor_pos, cursor);

  size_t pos = pos_dist(rng);
  cursor = 0;
  return replaceSubtreeAt(expr, pos, subtree, cursor);
}

double candidateFitness(const SyGuSProgram& program, const SynthFun& synth_fun,
                        const SExpr& candidate,
                        const std::vector<ValueEnvironment>& samples) {
  DefineFunMap define_funs;
  for (const auto& define_fun : program.define_funs) {
    define_funs.emplace(define_fun.name, &define_fun);
  }
  EvaluationContext context{&define_funs, &synth_fun, &candidate};

  size_t satisfied = 0;
  size_t total = 0;
  for (const auto& sample : samples) {
    for (const auto& constraint : program.constraints) {
      ++total;
      try {
        const Value result =
            evaluateExpression(constraint.expression, sample, context);
        if (result.kind() == ValueKind::Bool && result.asBool()) {
          ++satisfied;
        }
      } catch (...) {
      }
    }
  }
  return total > 0 ? static_cast<double>(satisfied) / static_cast<double>(total)
                   : 0.0;
}

SExpr makeSortExpr(const SortDescriptor& sort) {
  switch (sort.kind) {
    case ValueKind::Int:
      return SExpr::makeAtom("Int");
    case ValueKind::Bool:
      return SExpr::makeAtom("Bool");
    case ValueKind::BitVec:
      return SExpr::makeList({SExpr::makeAtom("_"), SExpr::makeAtom("BitVec"),
                              SExpr::makeAtom(std::to_string(sort.bit_width))});
  }
  return SExpr::makeAtom("Int");
}

std::vector<GrammarRule> generateDefaultGrammar(
    const SynthFun& synth_fun, const std::string& logic) {
  const auto return_sort = parseSort(synth_fun.return_sort);
  if (!return_sort.has_value()) return {};

  std::set<std::string> param_sort_strs;
  std::vector<SortDescriptor> param_sorts;
  for (const auto& param : synth_fun.params) {
    auto sort = parseSort(param.sort);
    if (sort.has_value()) {
      std::string key = sort->toString();
      if (param_sort_strs.insert(key).second) {
        param_sorts.push_back(*sort);
      }
    }
  }

  bool has_int = false;
  bool has_bool = false;
  bool has_bv = false;
  uint32_t bv_width = 0;

  if (return_sort->kind == ValueKind::Int) has_int = true;
  if (return_sort->kind == ValueKind::Bool) has_bool = true;
  if (return_sort->kind == ValueKind::BitVec) {
    has_bv = true;
    bv_width = return_sort->bit_width;
  }
  for (const auto& ps : param_sorts) {
    if (ps.kind == ValueKind::Int) has_int = true;
    if (ps.kind == ValueKind::Bool) has_bool = true;
    if (ps.kind == ValueKind::BitVec) {
      has_bv = true;
      bv_width = ps.bit_width;
    }
  }

  bool is_lia = logic.find("LIA") != std::string::npos ||
                logic.find("NIA") != std::string::npos ||
                logic.find("DTLIA") != std::string::npos;
  bool is_bv = logic.find("BV") != std::string::npos;

  if (!is_lia && !is_bv && has_int) is_lia = true;
  if (!is_lia && !is_bv && has_bv) is_bv = true;

  std::vector<GrammarRule> rules;

  auto addIntRule = [&]() {
    GrammarRule int_rule;
    int_rule.non_terminal = "ntInt";
    int_rule.sort = SExpr::makeAtom("Int");
    int_rule.type = "Int";

    int_rule.productions.push_back(SExpr::makeAtom("0"));
    int_rule.productions.push_back(SExpr::makeAtom("1"));

    for (const auto& param : synth_fun.params) {
      auto sort = parseSort(param.sort);
      if (sort.has_value() && sort->kind == ValueKind::Int) {
        int_rule.productions.push_back(SExpr::makeAtom(param.name));
      }
    }

    int_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("+"), SExpr::makeAtom("ntInt"),
         SExpr::makeAtom("ntInt")}));
    int_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("-"), SExpr::makeAtom("ntInt"),
         SExpr::makeAtom("ntInt")}));
    int_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("*"), SExpr::makeAtom("ntInt"),
         SExpr::makeAtom("ntInt")}));
    int_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("ite"), SExpr::makeAtom("ntBool"),
         SExpr::makeAtom("ntInt"), SExpr::makeAtom("ntInt")}));

    rules.push_back(std::move(int_rule));
  };

  auto addBoolRule = [&]() {
    GrammarRule bool_rule;
    bool_rule.non_terminal = "ntBool";
    bool_rule.sort = SExpr::makeAtom("Bool");
    bool_rule.type = "Bool";

    bool_rule.productions.push_back(SExpr::makeAtom("true"));
    bool_rule.productions.push_back(SExpr::makeAtom("false"));

    for (const auto& param : synth_fun.params) {
      auto sort = parseSort(param.sort);
      if (sort.has_value() && sort->kind == ValueKind::Bool) {
        bool_rule.productions.push_back(SExpr::makeAtom(param.name));
      }
    }

    bool_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("and"), SExpr::makeAtom("ntBool"),
         SExpr::makeAtom("ntBool")}));
    bool_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("or"), SExpr::makeAtom("ntBool"),
         SExpr::makeAtom("ntBool")}));
    bool_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("not"), SExpr::makeAtom("ntBool")}));
    bool_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("=>"), SExpr::makeAtom("ntBool"),
         SExpr::makeAtom("ntBool")}));

    if (has_int) {
      bool_rule.productions.push_back(SExpr::makeList(
          {SExpr::makeAtom("<="), SExpr::makeAtom("ntInt"),
           SExpr::makeAtom("ntInt")}));
      bool_rule.productions.push_back(SExpr::makeList(
          {SExpr::makeAtom("<"), SExpr::makeAtom("ntInt"),
           SExpr::makeAtom("ntInt")}));
      bool_rule.productions.push_back(SExpr::makeList(
          {SExpr::makeAtom("="), SExpr::makeAtom("ntInt"),
           SExpr::makeAtom("ntInt")}));
      bool_rule.productions.push_back(SExpr::makeList(
          {SExpr::makeAtom(">="), SExpr::makeAtom("ntInt"),
           SExpr::makeAtom("ntInt")}));
      bool_rule.productions.push_back(SExpr::makeList(
          {SExpr::makeAtom(">"), SExpr::makeAtom("ntInt"),
           SExpr::makeAtom("ntInt")}));
    }

    if (has_bv) {
      bool_rule.productions.push_back(SExpr::makeList(
          {SExpr::makeAtom("="), SExpr::makeAtom("ntBV"),
           SExpr::makeAtom("ntBV")}));
      bool_rule.productions.push_back(SExpr::makeList(
          {SExpr::makeAtom("bvult"), SExpr::makeAtom("ntBV"),
           SExpr::makeAtom("ntBV")}));
      bool_rule.productions.push_back(SExpr::makeList(
          {SExpr::makeAtom("bvslt"), SExpr::makeAtom("ntBV"),
           SExpr::makeAtom("ntBV")}));
    }

    rules.push_back(std::move(bool_rule));
  };

  auto addBVRule = [&]() {
    GrammarRule bv_rule;
    bv_rule.non_terminal = "ntBV";
    SExpr bv_sort = makeSortExpr(SortDescriptor{ValueKind::BitVec, bv_width});
    bv_rule.sort = bv_sort;
    bv_rule.type = bv_sort.toString();

    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("_"), SExpr::makeAtom("bv0"),
         SExpr::makeAtom(std::to_string(bv_width))}));
    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("_"), SExpr::makeAtom("bv1"),
         SExpr::makeAtom(std::to_string(bv_width))}));

    for (const auto& param : synth_fun.params) {
      auto sort = parseSort(param.sort);
      if (sort.has_value() && sort->kind == ValueKind::BitVec &&
          sort->bit_width == bv_width) {
        bv_rule.productions.push_back(SExpr::makeAtom(param.name));
      }
    }

    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("bvadd"), SExpr::makeAtom("ntBV"),
         SExpr::makeAtom("ntBV")}));
    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("bvsub"), SExpr::makeAtom("ntBV"),
         SExpr::makeAtom("ntBV")}));
    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("bvand"), SExpr::makeAtom("ntBV"),
         SExpr::makeAtom("ntBV")}));
    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("bvor"), SExpr::makeAtom("ntBV"),
         SExpr::makeAtom("ntBV")}));
    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("bvxor"), SExpr::makeAtom("ntBV"),
         SExpr::makeAtom("ntBV")}));
    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("bvnot"), SExpr::makeAtom("ntBV")}));
    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("bvneg"), SExpr::makeAtom("ntBV")}));
    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("bvshl"), SExpr::makeAtom("ntBV"),
         SExpr::makeAtom("ntBV")}));
    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("bvlshr"), SExpr::makeAtom("ntBV"),
         SExpr::makeAtom("ntBV")}));
    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("bvashr"), SExpr::makeAtom("ntBV"),
         SExpr::makeAtom("ntBV")}));
    bv_rule.productions.push_back(SExpr::makeList(
        {SExpr::makeAtom("ite"), SExpr::makeAtom("ntBool"),
         SExpr::makeAtom("ntBV"), SExpr::makeAtom("ntBV")}));

    rules.push_back(std::move(bv_rule));
  };

  if (return_sort->kind == ValueKind::Int) {
    addIntRule();
    addBoolRule();
    if (has_bv) addBVRule();
  } else if (return_sort->kind == ValueKind::Bool) {
    if (has_int) addIntRule();
    addBoolRule();
    if (has_bv) addBVRule();
  } else if (return_sort->kind == ValueKind::BitVec) {
    if (has_int) addIntRule();
    addBoolRule();
    addBVRule();
  }

  return rules;
}

void flattenExprTokens(const SExpr& expr, std::vector<std::string>& output);

SyGuSProgram convertInvToSynthFun(const SyGuSProgram& original) {
  if (original.synth_invs.empty() && original.inv_constraints.empty()) {
    return original;
  }

  SyGuSProgram program = original;
  program.synth_invs.clear();
  program.inv_constraints.clear();

  for (const auto& pv : original.declare_primed_vars) {
    bool found = false;
    for (const auto& dv : program.declare_vars) {
      if (dv.name == pv.name) { found = true; break; }
    }
    if (!found) {
      DeclareVar dv;
      dv.name = pv.name;
      dv.sort = pv.sort;
      dv.type = pv.type;
      program.declare_vars.push_back(std::move(dv));
    }
    std::string primed_name = pv.name + "!";
    bool found_primed = false;
    for (const auto& dv : program.declare_vars) {
      if (dv.name == primed_name) { found_primed = true; break; }
    }
    if (!found_primed) {
      DeclareVar dv;
      dv.name = primed_name;
      dv.sort = pv.sort;
      dv.type = pv.type;
      program.declare_vars.push_back(std::move(dv));
    }
  }
  program.declare_primed_vars.clear();

  for (const auto& synth_inv : original.synth_invs) {
    SynthFun synth_fun;
    synth_fun.name = synth_inv.name;
    synth_fun.params = synth_inv.params;
    synth_fun.return_sort = SExpr::makeAtom("Bool");
    synth_fun.return_type = "Bool";
    program.synth_funs.push_back(std::move(synth_fun));
  }

  for (const auto& inv_c : original.inv_constraints) {
    const DefineFun* pre_fun = nullptr;
    const DefineFun* trans_fun = nullptr;
    const DefineFun* post_fun = nullptr;

    for (const auto& df : program.define_funs) {
      if (df.name == inv_c.pre) pre_fun = &df;
      if (df.name == inv_c.trans) trans_fun = &df;
      if (df.name == inv_c.post) post_fun = &df;
    }

    if (!pre_fun || !trans_fun || !post_fun) continue;

    const SynthInv* inv = nullptr;
    for (const auto& si : original.synth_invs) {
      if (si.name == inv_c.inv) { inv = &si; break; }
    }
    if (!inv) continue;

    auto makeCallArgs = [](const std::string& fn,
                           const std::vector<TypedIdentifier>& params) {
      std::vector<SExpr> call;
      call.push_back(SExpr::makeAtom(fn));
      for (const auto& p : params) {
        call.push_back(SExpr::makeAtom(p.name));
      }
      return SExpr::makeList(std::move(call));
    };

    SExpr inv_call = makeCallArgs(inv_c.inv, inv->params);

    // pre(x) => inv(x)
    {
      Constraint c;
      c.expression = SExpr::makeList(
          {SExpr::makeAtom("=>"), pre_fun->body, inv_call});
      flattenExprTokens(c.expression, c.expr);
      program.constraints.push_back(std::move(c));
    }

    // inv(x) /\ trans(x,x') => inv(x')
    {
      std::vector<SExpr> primed_args;
      primed_args.push_back(SExpr::makeAtom(inv_c.inv));
      for (const auto& p : inv->params) {
        primed_args.push_back(SExpr::makeAtom(p.name + "!"));
      }
      SExpr inv_primed = SExpr::makeList(std::move(primed_args));

      Constraint c;
      c.expression = SExpr::makeList(
          {SExpr::makeAtom("=>"),
           SExpr::makeList({SExpr::makeAtom("and"), inv_call, trans_fun->body}),
           inv_primed});
      flattenExprTokens(c.expression, c.expr);
      program.constraints.push_back(std::move(c));

      for (const auto& p : inv->params) {
        bool already_declared = false;
        for (const auto& dv : program.declare_vars) {
          if (dv.name == p.name + "!") { already_declared = true; break; }
        }
        if (!already_declared) {
          DeclareVar dv;
          dv.name = p.name + "!";
          dv.sort = p.sort;
          dv.type = p.type;
          program.declare_vars.push_back(std::move(dv));
        }
      }
    }

    // inv(x) => post(x)
    {
      Constraint c;
      c.expression = SExpr::makeList(
          {SExpr::makeAtom("=>"), inv_call, post_fun->body});
      flattenExprTokens(c.expression, c.expr);
      program.constraints.push_back(std::move(c));
    }

    for (const auto& p : inv->params) {
      bool already_declared = false;
      for (const auto& dv : program.declare_vars) {
        if (dv.name == p.name) { already_declared = true; break; }
      }
      if (!already_declared) {
        DeclareVar dv;
        dv.name = p.name;
        dv.sort = p.sort;
        dv.type = p.type;
        program.declare_vars.push_back(std::move(dv));
      }
    }
  }

  return program;
}

void flattenExprTokens(const SExpr& expr, std::vector<std::string>& output) {
  if (expr.isAtom()) {
    output.push_back(expr.asAtom());
    return;
  }
  output.push_back("(");
  for (const auto& item : expr.asList()) {
    flattenExprTokens(item, output);
  }
  output.push_back(")");
}

std::string strategyToString(SearchStrategy strategy) {
  switch (strategy) {
    case SearchStrategy::Enum: return "enum";
    case SearchStrategy::BestFirst: return "best-first";
    case SearchStrategy::GA: return "ga";
    case SearchStrategy::SA: return "sa";
  }
  return "unknown";
}

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

SolveResult SyGuSSolver::solve(const SyGuSProgram& original_program,
                               const SolveOptions& options) const {
  SolveResult result;
  result.logic = original_program.logic;
  result.cvc5_available = hasCvc5();

  SyGuSProgram program = original_program;

  if (!program.synth_invs.empty() || !program.inv_constraints.empty()) {
    program = convertInvToSynthFun(program);
    if (options.verbose) {
      std::cerr << "[inv] converted synth-inv to synth-fun with "
                << program.constraints.size() << " constraints\n";
    }
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

  SynthFun& synth_fun = program.synth_funs.front();

  if (synth_fun.grammar_rules.empty()) {
    auto generated = generateDefaultGrammar(synth_fun, program.logic);
    if (generated.empty()) {
      result.status = SolveResult::Status::Unsupported;
      result.message =
          "Cannot generate a default grammar for this problem.";
      return result;
    }
    synth_fun.grammar_rules = std::move(generated);
    if (options.verbose) {
      std::cerr << "[grammar] auto-generated " << synth_fun.grammar_rules.size()
                << " grammar rules\n";
    }
  }

  const auto return_sort = parseSort(synth_fun.return_sort);
  if (!return_sort.has_value()) {
    result.status = SolveResult::Status::Unsupported;
    result.message =
        "Unsupported synth-fun return sort: " + synth_fun.return_type;
    return result;
  }

  if (return_sort->kind != ValueKind::Int &&
      return_sort->kind != ValueKind::Bool &&
      return_sort->kind != ValueKind::BitVec) {
    result.status = SolveResult::Status::Unsupported;
    result.message =
        "Only Int, Bool, and BitVec synth-fun return sorts are supported.";
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

  std::vector<ValueEnvironment> seed_samples =
      buildAssignments(asTypedIdentifiers(program.declare_vars), sample_pool,
                       std::max<size_t>(1, options.max_sample_assignments));

  if (param_samples.empty() || seed_samples.empty()) {
    result.status = SolveResult::Status::Unsupported;
    result.message =
        "Unable to build sample assignments for the current benchmark.";
    return result;
  }

  // CEGIS counterexample set — seeded with sample assignments, then grown by
  // counterexamples extracted from failed SMT verification rounds.
  std::vector<ValueEnvironment> counterexamples = seed_samples;

  // Bottom-up enumeration bank, shared across CEGIS rounds.
  std::unordered_map<std::string,
                     std::unordered_map<size_t, std::vector<SExpr>>>
      expressions_by_symbol_and_size;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      seen_signatures_by_symbol;

  // Pre-enumerate all expressions up to the size budget.
  std::vector<SExpr> candidate_pool;

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

        if (rule.non_terminal == start_symbol) {
          candidate_pool.push_back(expr);
        }
      }
    }
  }

  result.enumerated_candidates = candidate_pool.size();

  if (options.verbose) {
    std::cerr << "[enum] " << candidate_pool.size()
              << " candidates (max size " << options.max_expression_size << ")\n";
  }

  CandidatePredictor predictor;
  if (!options.model_path.empty()) {
    predictor.loadModel(options.model_path);
  }

  std::vector<std::string> param_names;
  for (const auto& param : synth_fun.params) {
    param_names.push_back(param.name);
  }

  if (predictor.isLoaded()) {
    std::vector<SExpr> filtered;
    for (const auto& expr : candidate_pool) {
      if (predictor.predict(expr, param_names)) {
        filtered.push_back(expr);
      }
    }
    result.ml_filtered_candidates = candidate_pool.size() - filtered.size();
    candidate_pool = std::move(filtered);
    if (options.verbose) {
      std::cerr << "[ml] filtered " << result.ml_filtered_candidates
                << ", kept " << candidate_pool.size() << "\n";
    }
  }

  result.strategy_name = strategyToString(options.strategy);

  // Best-first: sort candidates by predictor score descending
  if (options.strategy == SearchStrategy::BestFirst && predictor.isLoaded()) {
    std::vector<std::pair<double, size_t>> scored;
    scored.reserve(candidate_pool.size());
    for (size_t i = 0; i < candidate_pool.size(); ++i) {
      scored.emplace_back(predictor.score(candidate_pool[i], param_names), i);
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<SExpr> sorted_pool;
    sorted_pool.reserve(candidate_pool.size());
    for (const auto& [score, idx] : scored) {
      sorted_pool.push_back(std::move(candidate_pool[idx]));
    }
    candidate_pool = std::move(sorted_pool);
  }

  // GA strategy: evolve a population and test the best individuals
  if (options.strategy == SearchStrategy::GA) {
    if (candidate_pool.empty()) {
      result.status = SolveResult::Status::Exhausted;
      result.message = "No candidates to seed GA population.";
      return result;
    }

    std::mt19937 rng(42);
    const size_t pop_size = std::min(options.ga_population_size,
                                     std::max(candidate_pool.size(), size_t{2}));

    // Initialize population from candidate pool
    std::vector<SExpr> population;
    population.reserve(pop_size);
    std::uniform_int_distribution<size_t> pool_dist(0, candidate_pool.size() - 1);
    for (size_t i = 0; i < pop_size; ++i) {
      population.push_back(candidate_pool[pool_dist(rng)]);
    }

    std::uniform_real_distribution<double> prob(0.0, 1.0);

    if (options.verbose) {
      std::cerr << "[ga] population=" << pop_size
                << " pool=" << candidate_pool.size()
                << " generations=" << options.ga_generations << "\n";
    }

    for (size_t gen = 0; gen < options.ga_generations; ++gen) {
      ++result.ga_generations_used;

      // Evaluate fitness
      std::vector<double> fitness(pop_size);
      double best_fitness = 0.0;
      for (size_t i = 0; i < pop_size; ++i) {
        fitness[i] = candidateFitness(program, synth_fun, population[i],
                                       counterexamples);
        if (fitness[i] > best_fitness) best_fitness = fitness[i];
      }

      if (options.verbose && (gen % 10 == 0 || best_fitness >= 1.0)) {
        size_t best_idx = 0;
        for (size_t i = 1; i < pop_size; ++i) {
          if (fitness[i] > fitness[best_idx]) best_idx = i;
        }
        std::cerr << "[ga] gen=" << gen
                  << " best_fitness=" << best_fitness
                  << " best=" << population[best_idx].toString() << "\n";
      }

      // Check for perfect fitness
      for (size_t i = 0; i < pop_size; ++i) {
        if (fitness[i] < 1.0) continue;

        std::string error;
        if (!satisfiesConstraintsOnSamples(program, synth_fun, population[i],
                                            counterexamples, error)) {
          continue;
        }

        ++result.tested_candidates;

        if (!options.require_cvc5_verification) {
          result.status = SolveResult::Status::Solved;
          result.solution = population[i].toString();
          result.define_fun = renderDefineFun(synth_fun, population[i]);
          result.message = "GA found candidate (sample-validated only).";
          return result;
        }

#if SYGUS_HAVE_CVC5
        VerifyResult verify =
            verifyWithCvc5(program, synth_fun, population[i]);

        if (verify.status == VerifyStatus::Valid) {
          result.status = SolveResult::Status::Solved;
          result.solution = population[i].toString();
          result.define_fun = renderDefineFun(synth_fun, population[i]);
          result.cvc5_verified = true;
          return result;
        }

        if (verify.status == VerifyStatus::Counterexample) {
          counterexamples.push_back(std::move(verify.counterexample));
          ++result.counterexamples_found;
        }
#else
        result.status = SolveResult::Status::Solved;
        result.solution = population[i].toString();
        result.define_fun = renderDefineFun(synth_fun, population[i]);
        result.message =
            "cvc5 verification was requested but this build has no cvc5; "
            "the candidate is sample-validated only.";
        return result;
#endif
      }

      // Tournament selection + crossover + mutation
      std::vector<SExpr> next_gen;
      next_gen.reserve(pop_size);

      // Elitism: keep top 2
      std::vector<size_t> indices(pop_size);
      std::iota(indices.begin(), indices.end(), 0);
      std::partial_sort(indices.begin(),
                        indices.begin() + std::min(size_t{2}, pop_size),
                        indices.end(),
                        [&](size_t a, size_t b) {
                          return fitness[a] > fitness[b];
                        });
      for (size_t i = 0; i < std::min(size_t{2}, pop_size); ++i) {
        next_gen.push_back(population[indices[i]]);
      }

      auto tournamentSelect = [&]() -> size_t {
        std::uniform_int_distribution<size_t> dist(0, pop_size - 1);
        size_t a = dist(rng), b = dist(rng);
        return fitness[a] >= fitness[b] ? a : b;
      };

      while (next_gen.size() < pop_size) {
        size_t p1 = tournamentSelect();
        size_t p2 = tournamentSelect();

        SExpr child = (prob(rng) < options.ga_crossover_rate)
                          ? crossover(population[p1], population[p2], rng)
                          : population[p1];

        if (prob(rng) < options.ga_mutation_rate) {
          child = mutateExpr(child, candidate_pool, rng);
        }

        next_gen.push_back(std::move(child));
      }

      population = std::move(next_gen);
    }

    result.status = SolveResult::Status::Exhausted;
    result.message =
        "GA exhausted " + std::to_string(options.ga_generations) +
        " generations without finding a valid solution.";
    return result;
  }

  // SA strategy: single-trajectory search with Metropolis acceptance
  if (options.strategy == SearchStrategy::SA) {
    if (candidate_pool.empty()) {
      result.status = SolveResult::Status::Exhausted;
      result.message = "No candidates to seed SA.";
      return result;
    }

    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> pool_dist(0, candidate_pool.size() - 1);
    std::uniform_real_distribution<double> prob(0.0, 1.0);

    SExpr current = candidate_pool[pool_dist(rng)];
    double current_fitness = candidateFitness(program, synth_fun, current, counterexamples);
    SExpr best = current;
    double best_fitness = current_fitness;
    double temp = options.sa_initial_temp;

    if (options.verbose) {
      std::cerr << "[sa] initial_temp=" << temp
                << " cooling=" << options.sa_cooling_rate
                << " max_steps=" << options.sa_max_steps
                << " pool=" << candidate_pool.size() << "\n";
    }

    for (size_t step = 0; step < options.sa_max_steps && temp > 1e-6; ++step) {
      SExpr neighbor = mutateExpr(current, candidate_pool, rng);
      double neighbor_fitness = candidateFitness(program, synth_fun, neighbor, counterexamples);

      double delta = neighbor_fitness - current_fitness;
      if (delta > 0 || prob(rng) < std::exp(delta * 100.0 / temp)) {
        current = std::move(neighbor);
        current_fitness = neighbor_fitness;
      }

      if (current_fitness > best_fitness) {
        best = current;
        best_fitness = current_fitness;
      }

      if (options.verbose && (step % 100 == 0 || best_fitness >= 1.0)) {
        std::cerr << "[sa] step=" << step
                  << " temp=" << temp
                  << " best_fitness=" << best_fitness
                  << " current_fitness=" << current_fitness
                  << " best=" << best.toString() << "\n";
      }

      if (best_fitness >= 1.0) {
        std::string error;
        if (satisfiesConstraintsOnSamples(program, synth_fun, best,
                                           counterexamples, error)) {
          ++result.tested_candidates;

          if (!options.require_cvc5_verification) {
            result.status = SolveResult::Status::Solved;
            result.solution = best.toString();
            result.define_fun = renderDefineFun(synth_fun, best);
            result.message = "SA found candidate (sample-validated only).";
            return result;
          }

#if SYGUS_HAVE_CVC5
          VerifyResult verify = verifyWithCvc5(program, synth_fun, best);

          if (verify.status == VerifyStatus::Valid) {
            if (options.verbose) {
              std::cerr << "[sa] VERIFIED at step " << step << ": "
                        << best.toString() << "\n";
            }
            result.status = SolveResult::Status::Solved;
            result.solution = best.toString();
            result.define_fun = renderDefineFun(synth_fun, best);
            result.cvc5_verified = true;
            return result;
          }

          if (verify.status == VerifyStatus::Counterexample) {
            counterexamples.push_back(std::move(verify.counterexample));
            ++result.counterexamples_found;
            best_fitness = candidateFitness(program, synth_fun, best, counterexamples);
            current_fitness = candidateFitness(program, synth_fun, current, counterexamples);
            if (options.verbose) {
              std::cerr << "[sa] counterexample added, refitting ("
                        << counterexamples.size() << " total)\n";
            }
          }
#else
          result.status = SolveResult::Status::Solved;
          result.solution = best.toString();
          result.define_fun = renderDefineFun(synth_fun, best);
          result.message =
              "cvc5 verification was requested but this build has no cvc5; "
              "the candidate is sample-validated only.";
          return result;
#endif
        }
      }

      temp *= options.sa_cooling_rate;
    }

    result.status = SolveResult::Status::Exhausted;
    result.message =
        "SA exhausted " + std::to_string(options.sa_max_steps) +
        " steps without finding a valid solution (best fitness: " +
        std::to_string(best_fitness) + ").";
    return result;
  }

  if (options.verbose) {
    std::cerr << "[cegis] starting " << result.strategy_name
              << " with " << candidate_pool.size() << " candidates, "
              << counterexamples.size() << " counterexamples\n";
  }

  // Enum and BestFirst strategies share the same CEGIS loop
  // (BestFirst just has a better candidate ordering from the sort above)
  for (size_t round = 0; round < options.max_cegis_rounds; ++round) {
    ++result.cegis_rounds;

    const SExpr* best_candidate = nullptr;
    for (const auto& expr : candidate_pool) {
      std::string sample_error;
      if (satisfiesConstraintsOnSamples(program, synth_fun, expr,
                                        counterexamples, sample_error)) {
        best_candidate = &expr;
        break;
      }
    }

    if (best_candidate == nullptr) {
      if (options.verbose) {
        std::cerr << "[cegis] round " << round
                  << ": no candidate satisfies all counterexamples\n";
      }
      result.status = SolveResult::Status::Exhausted;
      result.message =
          "No candidate in the enumeration bank satisfies all counterexamples.";
      return result;
    }

    ++result.tested_candidates;

    if (options.verbose) {
      std::cerr << "[cegis] round " << round
                << ": testing " << best_candidate->toString() << "\n";
    }

    if (!options.require_cvc5_verification) {
      result.status = SolveResult::Status::Solved;
      result.solution = best_candidate->toString();
      result.define_fun = renderDefineFun(synth_fun, *best_candidate);
      result.message = "Sample-validated only (no SMT verification).";
      return result;
    }

#if SYGUS_HAVE_CVC5
    VerifyResult verify = verifyWithCvc5(program, synth_fun, *best_candidate);

    if (verify.status == VerifyStatus::Valid) {
      if (options.verbose) {
        std::cerr << "[cegis] VERIFIED: " << best_candidate->toString() << "\n";
      }
      result.status = SolveResult::Status::Solved;
      result.solution = best_candidate->toString();
      result.define_fun = renderDefineFun(synth_fun, *best_candidate);
      result.cvc5_verified = true;
      return result;
    }

    if (verify.status == VerifyStatus::Counterexample) {
      if (options.verbose) {
        std::cerr << "[cegis] counterexample found, adding to set ("
                  << counterexamples.size() + 1 << " total)\n";
      }
      counterexamples.push_back(std::move(verify.counterexample));
      ++result.counterexamples_found;
      continue;
    }

    result.status = SolveResult::Status::Error;
    result.message = verify.message;
    return result;
#else
    result.status = SolveResult::Status::Solved;
    result.solution = best_candidate->toString();
    result.define_fun = renderDefineFun(synth_fun, *best_candidate);
    result.message =
        "cvc5 verification was requested but this build has no cvc5; "
        "the candidate is sample-validated only.";
    return result;
#endif
  }

  result.status = SolveResult::Status::Exhausted;
  result.message = "Reached the CEGIS round limit (" +
                   std::to_string(options.max_cegis_rounds) +
                   ") without finding a valid solution.";
  return result;
}
