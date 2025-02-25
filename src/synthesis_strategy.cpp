#include "synthesis_strategy.hpp"
#include <ranges>


int extractBitVecSize(std::string s ) {
    // bullshit; rework

    auto split = s
        | std::ranges::views::split(' ')
        | std::ranges::views::transform([](auto&& str) { return std::string_view(&*str.begin(), std::ranges::distance(str)); });

    int i = 0;
    for (auto&& word : split)
    {
        i++;
        std::cout << word << std::endl;
        std::string lr {word};
        if (i == 2 ) return std::stoi(lr);
    }

}

bool SynthesisStrategy::verifyCandidate(
        const std::shared_ptr<GrammarTerm>& candidate,
        const SyGuSProgram& program,
        cvc5::TermManager& tm,
        cvc5::Solver& solver,
        std::vector<cvc5::Term>& counterexamples
    ){
        // Reset assertions
        solver.resetAssertions();

        // 1. Convert SyGuS variables to CVC5 terms
        std::map<std::string, cvc5::Term> var_map;
        for (const auto& var : program.declare_vars) {
            if (var.type == "Int") {
                cvc5::Sort sort = tm.getIntegerSort();
                var_map[var.name] = tm.mkConst(sort, var.name);
            } else if (var.type.starts_with("(_ BitVec")) {
                int size = extractBitVecSize(var.type);
                cvc5::Sort sort = tm.mkBitVectorSort(size);
                var_map[var.name] = tm.mkConst(sort, var.name);
            }
        }

        // 2. Convert candidate to CVC5 term
        cvc5::Term candidate_term = termToCVC5(candidate,tm,solver, var_map);

        // 3. Add constraints to the solver
        for (const auto& constraint : program.constraints) {
            cvc5::Term constraint_term = parseConstraintToCVC5(constraint.expr,tm,solver, var_map);
            solver.assertFormula(constraint_term);
        }

        // 4. Check satisfiability
        cvc5::Result result = solver.checkSat();
        if (result.isSat()) {
            // Extract counterexample from the model
            cvc5::Term model = solver.getValue(candidate_term);
            counterexamples.push_back(model);
            return false;
        } else if (result.isUnsat()) {
            return true;  // Candidate is valid
        } else {
            throw std::runtime_error("Solver returned unknown");
        }
    }

bool SynthesisStrategy::isNonTerminal(const std::string& sym, const SyGuSProgram& program) {
    return program.synth_funs[0].grammar.count(sym) > 0;
}

std::shared_ptr<GrammarTerm> SynthesisStrategy::buildFromProduction(
        const std::vector<std::shared_ptr<GrammarTerm>>& prod
    ) {
    auto root = std::make_shared<GrammarTerm>(prod[0]->symbol);
    for (size_t i=1; i<prod.size(); ++i)
        root->children.push_back(prod[i]);
    return root;
}
cvc5::Term SynthesisStrategy::termToCVC5(
    const std::shared_ptr<GrammarTerm>& term,
    cvc5::TermManager& tm,
    cvc5::Solver& solver,
    const std::map<std::string, cvc5::Term>& var_map
) {
     if (term->symbol == "bvadd") {
         return tm.mkTerm(cvc5::Kind::BITVECTOR_ADD, {
             termToCVC5(term->children[0], tm, solver, var_map),
             termToCVC5(term->children[1], tm, solver, var_map)
         });
     } else if (var_map.count(term->symbol)) {
         return var_map.at(term->symbol);
     } else {
         try {
             uint32_t val = std::stoul(term->symbol, nullptr, 16);  // Hex literals
             return tm.mkBitVector(8, val);  // Assume 8-bit
         } catch (...) {
             throw std::runtime_error("Unknown term: " + term->symbol);
         }
     }
 }


cvc5::Term SynthesisStrategy::parseTerm(
    const std::vector<std::string>& tokens,
    size_t& pos,
    cvc5::TermManager& tm,
    cvc5::Solver& solver,
    const std::map<std::string, cvc5::Term>& var_map
) {
    if (pos >= tokens.size()) {
        throw std::runtime_error("Unexpected end of constraint tokens");
    }
    std::string token = tokens[pos++];

    if (token == "(") {
        // Parse operator and arguments
        std::string op = tokens[pos++];
        std::vector<cvc5::Term> args;
        while (pos < tokens.size() && tokens[pos] != ")") {
            args.push_back(parseTerm(tokens, pos, tm, solver, var_map));
        }
        pos++; // Skip closing ')'

        // Handle function application (e.g., (f x))
        if (var_map.count(op)) {
            cvc5::Term func = var_map.at(op);
            if (func.getSort().isFunction()) {
                args.insert(args.begin(), func);
                return solver.mkTerm(cvc5::Kind::APPLY_UF, args);
            }
        }

        // Handle built-in operators
        cvc5::Kind kind = getKind(op);
        return solver.mkTerm(kind, args);
    } else {
        // Handle variables and literals
        if (var_map.count(token)) {
            return var_map.at(token);
        } else if (token[0] == '#') {
            // Handle BitVec literals (e.g., #x00, #b1010)
            if (token[1] == 'x') {
                uint32_t value = std::stoul(token.substr(2), nullptr, 16);
                return tm.mkBitVector(8, value); // Assume 8-bit for example
            } else if (token[1] == 'b') {
                uint32_t value = std::stoul(token.substr(2), nullptr, 2);
                return tm.mkBitVector(token.size() - 2, value);
            }
        }
        throw std::runtime_error("Unknown symbol or literal: " + token);
    }
}

cvc5::Kind SynthesisStrategy::getKind(const std::string& op) {
    static std::map<std::string, cvc5::Kind> op_map = {
        {"=", cvc5::Kind::EQUAL},
        {"bvadd", cvc5::Kind::BITVECTOR_ADD},
        {"bvmul", cvc5::Kind::BITVECTOR_MULT},
        {"and", cvc5::Kind::AND},
        {"or", cvc5::Kind::OR},
        {"not", cvc5::Kind::NOT}
        // Add more operators as needed
    };
    if (op_map.find(op) == op_map.end()) {
        throw std::runtime_error("Unsupported operator: " + op);
    }
    return op_map[op];
}

cvc5::Term SynthesisStrategy::parseConstraintToCVC5(
    const std::vector<std::string>& constraint_tokens,
    cvc5::TermManager& tm,
    cvc5::Solver& solver,
    const std::map<std::string, cvc5::Term>& var_map
) {
    size_t pos = 0;
    return parseTerm(constraint_tokens, pos, tm, solver, var_map);
}