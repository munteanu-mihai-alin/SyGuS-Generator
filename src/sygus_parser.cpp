#include "sygus_parser.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace {

struct TokenStream {
  const std::vector<std::string>& tokens;
  size_t position = 0;

  bool atEnd() const { return position >= tokens.size(); }

  const std::string& peek(const std::string& context) const {
    if (atEnd()) {
      throw std::runtime_error("Unexpected end of input while parsing " +
                               context);
    }
    return tokens[position];
  }

  std::string consume(const std::string& context) {
    const std::string value = peek(context);
    ++position;
    return value;
  }
};

void flushToken(std::string& current, std::vector<std::string>& tokens) {
  if (!current.empty()) {
    tokens.push_back(current);
    current.clear();
  }
}

std::vector<std::string> tokenize(const std::string& input) {
  std::vector<std::string> tokens;
  std::string current;
  bool in_string = false;
  bool in_quoted_symbol = false;
  bool escape_next = false;
  bool in_comment = false;

  for (char raw_char : input) {
    const unsigned char unsigned_char = static_cast<unsigned char>(raw_char);
    const char character = static_cast<char>(unsigned_char);

    if (in_comment) {
      if (character == '\n') {
        in_comment = false;
      }
      continue;
    }

    if (in_string) {
      current += character;
      if (escape_next) {
        escape_next = false;
        continue;
      }
      if (character == '\\') {
        escape_next = true;
        continue;
      }
      if (character == '"') {
        in_string = false;
        flushToken(current, tokens);
      }
      continue;
    }

    if (in_quoted_symbol) {
      current += character;
      if (character == '|') {
        in_quoted_symbol = false;
        flushToken(current, tokens);
      }
      continue;
    }

    if (character == ';') {
      flushToken(current, tokens);
      in_comment = true;
      continue;
    }

    if (character == '"') {
      flushToken(current, tokens);
      current += character;
      in_string = true;
      continue;
    }

    if (character == '|') {
      flushToken(current, tokens);
      current += character;
      in_quoted_symbol = true;
      continue;
    }

    if (std::isspace(unsigned_char)) {
      flushToken(current, tokens);
      continue;
    }

    if (character == '(' || character == ')') {
      flushToken(current, tokens);
      tokens.emplace_back(1, character);
      continue;
    }

    current += character;
  }

  if (in_string) {
    throw std::runtime_error("Unterminated string literal in SyGuS input");
  }
  if (in_quoted_symbol) {
    throw std::runtime_error("Unterminated quoted symbol in SyGuS input");
  }

  flushToken(current, tokens);
  return tokens;
}

SExpr parseSExpr(TokenStream& stream) {
  const std::string token = stream.consume("S-expression");
  if (token == "(") {
    std::vector<SExpr> items;
    while (stream.peek("list") != ")") {
      items.push_back(parseSExpr(stream));
    }
    stream.consume("closing ')'");
    return SExpr::makeList(std::move(items));
  }

  if (token == ")") {
    throw std::runtime_error("Unexpected ')' while parsing SyGuS input");
  }

  return SExpr::makeAtom(token);
}

const std::vector<SExpr>& expectList(const SExpr& expr,
                                     std::string_view context) {
  if (!expr.isList()) {
    throw std::runtime_error("Expected list for " + std::string(context) +
                             ", got " + expr.toString());
  }
  return expr.asList();
}

const std::string& expectAtom(const SExpr& expr, std::string_view context) {
  if (!expr.isAtom()) {
    throw std::runtime_error("Expected atom for " + std::string(context) +
                             ", got " + expr.toString());
  }
  return expr.asAtom();
}

std::string normalizeSortText(const SExpr& sort_expr) {
  if (sort_expr.isAtom()) {
    return sort_expr.asAtom();
  }

  const auto& items = sort_expr.asList();
  if (items.size() == 1 && items.front().isList()) {
    return normalizeSortText(items.front());
  }
  return sort_expr.toString();
}

TypedIdentifier parseTypedIdentifier(const SExpr& expr) {
  const auto& items = expectList(expr, "typed identifier");
  if (items.size() != 2) {
    throw std::runtime_error("Expected typed identifier '(name sort)', got " +
                             expr.toString());
  }

  TypedIdentifier identifier;
  identifier.name = expectAtom(items[0], "typed identifier name");
  identifier.sort = items[1];
  identifier.type = normalizeSortText(items[1]);
  return identifier;
}

std::shared_ptr<GrammarTerm> makeGrammarTree(const SExpr& expr) {
  if (expr.isAtom()) {
    return std::make_shared<GrammarTerm>(expr.asAtom());
  }

  const auto& items = expectList(expr, "grammar production");
  if (items.empty()) {
    throw std::runtime_error("Grammar production cannot be an empty list");
  }

  const std::string& head = expectAtom(items[0], "grammar operator");
  std::vector<std::shared_ptr<GrammarTerm>> children;
  children.reserve(items.size() - 1);
  for (size_t index = 1; index < items.size(); ++index) {
    children.push_back(makeGrammarTree(items[index]));
  }
  return std::make_shared<GrammarTerm>(head, std::move(children));
}

std::vector<std::shared_ptr<GrammarTerm>> makeLegacyProduction(
    const SExpr& expr) {
  if (expr.isAtom()) {
    return {std::make_shared<GrammarTerm>(expr.asAtom())};
  }

  auto tree = makeGrammarTree(expr);
  std::vector<std::shared_ptr<GrammarTerm>> flattened;
  flattened.push_back(std::make_shared<GrammarTerm>(tree->symbol));
  for (const auto& child : tree->children) {
    flattened.push_back(child);
  }
  return flattened;
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

DeclareVar parseDeclareVarCommand(const std::vector<SExpr>& items) {
  if (items.size() != 3) {
    throw std::runtime_error("declare-var expects exactly two arguments");
  }

  DeclareVar variable;
  variable.name = expectAtom(items[1], "declare-var name");
  variable.sort = items[2];
  variable.type = normalizeSortText(items[2]);
  return variable;
}

DeclarePrimedVar parseDeclarePrimedVarCommand(const std::vector<SExpr>& items) {
  if (items.size() != 3) {
    throw std::runtime_error(
        "declare-primed-var expects exactly two arguments");
  }

  DeclarePrimedVar variable;
  variable.name = expectAtom(items[1], "declare-primed-var name");
  variable.sort = items[2];
  variable.type = normalizeSortText(items[2]);
  return variable;
}

Constraint parseConstraintCommand(const std::vector<SExpr>& items) {
  if (items.size() != 2) {
    throw std::runtime_error("constraint expects exactly one expression");
  }

  Constraint constraint;
  constraint.expression = items[1];
  flattenExprTokens(constraint.expression, constraint.expr);
  return constraint;
}

DefineFun parseDefineFunCommand(const std::vector<SExpr>& items) {
  if (items.size() != 5) {
    throw std::runtime_error("define-fun expects four arguments");
  }

  DefineFun definition;
  definition.name = expectAtom(items[1], "define-fun name");

  const auto& params = expectList(items[2], "define-fun parameters");
  definition.params.reserve(params.size());
  for (const auto& param : params) {
    definition.params.push_back(parseTypedIdentifier(param));
  }

  definition.return_sort = items[3];
  definition.return_type = normalizeSortText(items[3]);
  definition.body = items[4];
  return definition;
}

SynthInv parseSynthInvCommand(const std::vector<SExpr>& items) {
  if (items.size() != 3) {
    throw std::runtime_error("synth-inv expects two arguments");
  }

  SynthInv synth_inv;
  synth_inv.name = expectAtom(items[1], "synth-inv name");

  const auto& params = expectList(items[2], "synth-inv parameters");
  synth_inv.params.reserve(params.size());
  for (const auto& param : params) {
    synth_inv.params.push_back(parseTypedIdentifier(param));
  }

  return synth_inv;
}

InvConstraint parseInvConstraintCommand(const std::vector<SExpr>& items) {
  if (items.size() != 5) {
    throw std::runtime_error("inv-constraint expects four symbol arguments");
  }

  InvConstraint constraint;
  constraint.inv = expectAtom(items[1], "inv-constraint invariant");
  constraint.pre = expectAtom(items[2], "inv-constraint pre-condition");
  constraint.trans = expectAtom(items[3], "inv-constraint transition relation");
  constraint.post = expectAtom(items[4], "inv-constraint post-condition");
  return constraint;
}

GrammarRule parseGrammarRule(const SExpr& rule_expr) {
  const auto& rule_items = expectList(rule_expr, "grammar rule");
  if (rule_items.size() != 3) {
    throw std::runtime_error(
        "Expected grammar rule '(NT Sort Productions)', got " +
        rule_expr.toString());
  }

  GrammarRule rule;
  rule.non_terminal = expectAtom(rule_items[0], "grammar non-terminal");
  rule.sort = rule_items[1];
  rule.type = normalizeSortText(rule_items[1]);

  const auto& production_items =
      expectList(rule_items[2], "grammar production list");
  rule.productions = production_items;
  return rule;
}

void addGrammarRule(const GrammarRule& rule, SynthFun& synth_fun) {
  synth_fun.grammar_rules.push_back(rule);

  std::vector<std::vector<std::shared_ptr<GrammarTerm>>> legacy_productions;
  legacy_productions.reserve(rule.productions.size());
  for (const auto& production_expr : rule.productions) {
    legacy_productions.push_back(makeLegacyProduction(production_expr));
  }
  synth_fun.grammar[rule.non_terminal] = std::move(legacy_productions);
}

void parseLegacySynthFunGrammar(const SExpr& grammar_expr,
                                SynthFun& synth_fun) {
  const auto& grammar_rules = expectList(grammar_expr, "synth-fun grammar");
  for (const auto& rule_expr : grammar_rules) {
    addGrammarRule(parseGrammarRule(rule_expr), synth_fun);
  }
}

void parseModernSynthFunGrammar(const SExpr& predeclaration_expr,
                                const SExpr& grammar_expr,
                                SynthFun& synth_fun) {
  const auto& predeclarations =
      expectList(predeclaration_expr, "synth-fun non-terminal declarations");
  std::unordered_map<std::string, std::string> declared_sorts;
  declared_sorts.reserve(predeclarations.size());

  for (const auto& decl_expr : predeclarations) {
    const auto& decl_items = expectList(decl_expr, "non-terminal declaration");
    if (decl_items.size() != 2) {
      throw std::runtime_error(
          "Expected non-terminal predeclaration '(NT Sort)', got " +
          decl_expr.toString());
    }
    const std::string& non_terminal =
        expectAtom(decl_items[0], "non-terminal predeclaration name");
    declared_sorts.emplace(non_terminal, normalizeSortText(decl_items[1]));
  }

  const auto& grammar_rules = expectList(grammar_expr, "synth-fun grammar");
  for (const auto& rule_expr : grammar_rules) {
    const GrammarRule rule = parseGrammarRule(rule_expr);
    if (const auto found = declared_sorts.find(rule.non_terminal);
        found != declared_sorts.end() && found->second != rule.type) {
      throw std::runtime_error(
          "Grammar rule sort does not match predeclaration for " +
          rule.non_terminal);
    }
    addGrammarRule(rule, synth_fun);
  }
}

SynthFun parseSynthFunCommand(const std::vector<SExpr>& items) {
  if (items.size() != 4 && items.size() != 5 && items.size() != 6) {
    throw std::runtime_error(
        "synth-fun expects either three, four, or five arguments");
  }

  SynthFun synth_fun;
  synth_fun.name = expectAtom(items[1], "synth-fun name");

  const auto& params = expectList(items[2], "synth-fun parameters");
  synth_fun.params.reserve(params.size());
  for (const auto& param : params) {
    synth_fun.params.push_back(parseTypedIdentifier(param));
  }

  synth_fun.return_sort = items[3];
  synth_fun.return_type = normalizeSortText(items[3]);

  if (items.size() == 5) {
    parseLegacySynthFunGrammar(items[4], synth_fun);
  } else if (items.size() == 6) {
    parseModernSynthFunGrammar(items[4], items[5], synth_fun);
  }

  return synth_fun;
}

SyGuSProgram buildProgram(const std::vector<SExpr>& forms) {
  SyGuSProgram program;
  program.top_level_forms = forms;

  for (const auto& form : forms) {
    const auto& items = expectList(form, "top-level command");
    if (items.empty()) {
      throw std::runtime_error("Top-level SyGuS command cannot be empty");
    }

    const std::string command = expectAtom(items[0], "command name");
    if (command == "set-logic") {
      if (items.size() != 2) {
        throw std::runtime_error("set-logic expects exactly one argument");
      }
      program.logic = normalizeSortText(items[1]);
    } else if (command == "synth-fun") {
      program.synth_funs.push_back(parseSynthFunCommand(items));
    } else if (command == "declare-var") {
      program.declare_vars.push_back(parseDeclareVarCommand(items));
    } else if (command == "declare-primed-var") {
      program.declare_primed_vars.push_back(
          parseDeclarePrimedVarCommand(items));
    } else if (command == "constraint") {
      program.constraints.push_back(parseConstraintCommand(items));
    } else if (command == "define-fun") {
      program.define_funs.push_back(parseDefineFunCommand(items));
    } else if (command == "synth-inv") {
      program.synth_invs.push_back(parseSynthInvCommand(items));
    } else if (command == "inv-constraint") {
      program.inv_constraints.push_back(parseInvConstraintCommand(items));
    } else if (command == "check-synth") {
      program.has_check_synth = true;
    } else {
      program.other_commands.push_back(RawCommand{command, form});
    }
  }

  return program;
}

}  // namespace

std::vector<SExpr> SyGuSParser::parseForms(const std::string& input) const {
  const auto tokens = tokenize(input);
  TokenStream stream{tokens};

  std::vector<SExpr> forms;
  while (!stream.atEnd()) {
    forms.push_back(parseSExpr(stream));
  }
  return forms;
}

SyGuSProgram SyGuSParser::parse(const std::string& input) const {
  return buildProgram(parseForms(input));
}

SyGuSProgram SyGuSParser::parseFile(const std::filesystem::path& path) const {
  std::ifstream input_stream(path);
  if (!input_stream) {
    throw std::runtime_error("Failed to open SyGuS file: " + path.string());
  }

  std::ostringstream buffer;
  buffer << input_stream.rdbuf();
  return parse(buffer.str());
}
