//
// Created by munte on 2/14/2025.
//

/*
#include "SyGuSSynthesizer.h"

std::shared_ptr<GrammarTerm> SyGuSSynthesizer::generateCandidate() {
    static size_t depth = 0;  // Increase depth over time
    std::vector<std::shared_ptr<GrammarTerm>> candidates;

    // Start with non-terminal "Start"
    for (const auto& prod : program.synth_funs[0].grammar["Start"]) {
        auto term = buildTermFromProduction(prod);
        candidates.push_back(term);
    }

    // Expand candidates iteratively (BFS)
    for (size_t d = 0; d < depth; d++) {
        std::vector<std::shared_ptr<GrammarTerm>> new_candidates;
        for (const auto& term : candidates) {
            // Replace non-terminals (e.g., "Start") with their productions
            expandTerm(term, new_candidates);
        }
        candidates = new_candidates;
    }

    depth++;  // Explore deeper terms on failure
    return candidates.empty() ? nullptr : candidates[0];
}

// Helper: Expand a term by replacing non-terminals with productions
void SyGuSSynthesizer::expandTerm(const std::shared_ptr<GrammarTerm>& term,
                std::vector<std::shared_ptr<GrammarTerm>>& new_terms) {
    for (auto& child : term->children) {
        if (child->symbol == "Start") {  // Non-terminal
            for (const auto& prod : program.synth_funs[0].grammar["Start"]) {
                auto new_term = replaceChild(term, child, buildTermFromProduction(prod));
                new_terms.push_back(new_term);
            }
        }
    }
}

// Build a GrammarTerm tree from a production rule (list of terms)

#include "SynthesisStrategy.h"

std::shared_ptr<GrammarTerm> SyGuSSynthesizer::buildTermFromProduction(
    const std::vector<std::shared_ptr<GrammarTerm>>& production
) {
    if (production.empty()) {
        return nullptr; // Handle empty productions (invalid grammar)
    }

    // First element is the root node (operator/terminal)
    auto root = std::make_shared<GrammarTerm>(production[0]->symbol);

    // Remaining elements are children
    for (size_t i = 1; i < production.size(); ++i) {
        root->children.push_back(production[i]);
    }

    return root;

}
*/

#include <memory>
#include "SynthesisStrategy.h"
#include "SyGuSParser.h"
class SyGuSSynthesizer {
public:
    SyGuSSynthesizer(
        std::unique_ptr<SynthesisStrategy> strategy,
        const SyGuSProgram& program,
        z3::context& ctx
    ) : strategy(std::move(strategy)), program(program), ctx(ctx) {}

    std::shared_ptr<GrammarTerm> synthesize() {
        std::vector<z3::expr> counterexamples;
        return strategy->synthesize(program, ctx, counterexamples);
    }

    void addCounterexample(const z3::expr& ce) {
        counterexamples.push_back(ce);
    }

private:
    std::unique_ptr<SynthesisStrategy> strategy;
    const SyGuSProgram& program;
    z3::context& ctx;
    std::vector<z3::expr> counterexamples;
};