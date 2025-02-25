#pragma once
#include "synthesis_strategy.hpp"

class BFSStrategy : public SynthesisStrategy {
public:
    std::shared_ptr<GrammarTerm> synthesize(
        const SyGuSProgram& program,
        cvc5::TermManager& tm,
        cvc5::Solver& solver,
        std::vector<cvc5::Term>& counterexamples
    ) override;
};