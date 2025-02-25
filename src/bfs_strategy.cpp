#include "bfs_strategy.hpp"
#include <queue>

using namespace cvc5;


std::shared_ptr<GrammarTerm> BFSStrategy::synthesize(
    const SyGuSProgram& program,
    cvc5::TermManager& tm,
    cvc5::Solver& solver,
    std::vector<cvc5::Term>& counterexamples
) {
    std::queue<std::shared_ptr<GrammarTerm>> q;
    const auto& start_prods = program.synth_funs[0].grammar.at("Start");
    for (const auto& prod : start_prods)
        q.push(buildFromProduction(prod));

    while (!q.empty()) {
        auto candidate = q.front();
        q.pop();

        if (SynthesisStrategy::verifyCandidate(candidate, program, tm, solver, counterexamples))
            return candidate;

        // Expand non-terminals
        for (auto& child : candidate->children) {
            if (isNonTerminal(child->symbol, program)) {
                for (const auto& prod : program.synth_funs[0].grammar.at(child->symbol)) {
                    auto new_child = buildFromProduction(prod);
                    auto new_term = std::make_shared<GrammarTerm>(*candidate);
                    std::replace(new_term->children.begin(), new_term->children.end(), child, new_child);
                    q.push(new_term);
                }
            }
        }
    }
    return nullptr;
}