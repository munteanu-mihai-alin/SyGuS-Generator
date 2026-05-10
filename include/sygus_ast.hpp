#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct SExpr {
  enum class Kind {
    Atom,
    List,
  };

  Kind kind = Kind::Atom;
  std::string atom;
  std::vector<SExpr> list;

  static SExpr makeAtom(std::string value) {
    SExpr expr;
    expr.kind = Kind::Atom;
    expr.atom = std::move(value);
    return expr;
  }

  static SExpr makeList(std::vector<SExpr> values) {
    SExpr expr;
    expr.kind = Kind::List;
    expr.list = std::move(values);
    return expr;
  }

  bool isAtom() const { return kind == Kind::Atom; }

  bool isList() const { return kind == Kind::List; }

  const std::string& asAtom() const {
    if (!isAtom()) {
      throw std::runtime_error("Expected atom S-expression");
    }
    return atom;
  }

  const std::vector<SExpr>& asList() const {
    if (!isList()) {
      throw std::runtime_error("Expected list S-expression");
    }
    return list;
  }

  std::string toString() const {
    if (isAtom()) {
      return atom;
    }

    std::string rendered = "(";
    for (size_t index = 0; index < list.size(); ++index) {
      if (index != 0) {
        rendered += " ";
      }
      rendered += list[index].toString();
    }
    rendered += ")";
    return rendered;
  }

  bool operator==(const SExpr&) const = default;
};

struct GrammarTerm {
  std::string symbol;
  std::vector<std::shared_ptr<GrammarTerm>> children;

  GrammarTerm() = default;

  explicit GrammarTerm(std::string sym) : symbol(std::move(sym)) {}

  GrammarTerm(std::string sym, std::vector<std::shared_ptr<GrammarTerm>> ch)
      : symbol(std::move(sym)), children(std::move(ch)) {}

  std::string toString() const {
    if (children.empty()) {
      return symbol;
    }

    std::string rendered = "(" + symbol;
    for (const auto& child : children) {
      rendered += " " + child->toString();
    }
    rendered += ")";
    return rendered;
  }
};

struct TypedIdentifier {
  std::string name;
  SExpr sort;
  std::string type;
};

struct GrammarRule {
  std::string non_terminal;
  SExpr sort;
  std::string type;
  std::vector<SExpr> productions;
};

struct SynthFun {
  std::string name;
  std::vector<TypedIdentifier> params;
  std::string return_type;
  SExpr return_sort;
  std::vector<GrammarRule> grammar_rules;
  std::map<std::string, std::vector<std::vector<std::shared_ptr<GrammarTerm>>>>
      grammar;
};

struct DeclareVar {
  std::string name;
  std::string type;
  SExpr sort;
};

struct DeclarePrimedVar {
  std::string name;
  std::string type;
  SExpr sort;
};

struct Constraint {
  std::vector<std::string> expr;
  SExpr expression;
};

struct DefineFun {
  std::string name;
  std::vector<TypedIdentifier> params;
  std::string return_type;
  SExpr return_sort;
  SExpr body;
};

struct SynthInv {
  std::string name;
  std::vector<TypedIdentifier> params;
};

struct InvConstraint {
  std::string inv;
  std::string pre;
  std::string trans;
  std::string post;
};

struct RawCommand {
  std::string name;
  SExpr form;
};

struct SyGuSProgram {
  std::string logic;
  std::vector<SynthFun> synth_funs;
  std::vector<DeclareVar> declare_vars;
  std::vector<DeclarePrimedVar> declare_primed_vars;
  std::vector<SynthInv> synth_invs;
  std::vector<Constraint> constraints;
  std::vector<DefineFun> define_funs;
  std::vector<InvConstraint> inv_constraints;
  bool has_check_synth = false;
  std::vector<RawCommand> other_commands;
  std::vector<SExpr> top_level_forms;
};
