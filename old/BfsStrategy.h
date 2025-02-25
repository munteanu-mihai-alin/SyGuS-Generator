//
// Created by munte on 2/16/2025.
//

#ifndef BFSSTRATEGY_H
#define BFSSTRATEGY_H
// bfs_strategy.hpp
#pragma once
#include "synthesis_strategy.hpp"

class BFSStrategy : public SynthesisStrategy {
public:
    std::shared_ptr<GrammarTerm> synthesize(
        const SyGuSProgram& program,
        z3::context& ctx,
        const std::vector<z3::expr>& counterexamples
    ) override {
        // BFS implementation from previous steps
        std::vector<std::shared_ptr<GrammarTerm>> candidates;
        size_t depth = 0;

        while (true) {
            // 1. Expand candidates using BFS
            // 2. Check candidates against counterexamples
            // 3. Return solution if found
        }
    }

private:
    std::shared_ptr<GrammarTerm> generateCandidate(...) { /* ... */ }
    void expandTerm(...) { /* ... */ }
};
#endif //BFSSTRATEGY_H
