#pragma once
#include "synthesis_strategy.hpp"
#include <memory>

class SyGuSSynthesizer {
public:
    SyGuSSynthesizer(
        std::unique_ptr<SynthesisStrategy> strategy,
        const SyGuSProgram& program,
        cvc5::TermManager& tm,
        cvc5::Solver& solver
    );
    std::shared_ptr<GrammarTerm> synthesize();

private:
    std::unique_ptr<SynthesisStrategy> strategy;
    SyGuSProgram program;
    cvc5::TermManager& tm;
    cvc5::Solver& solver;
    std::vector<cvc5::Term> counterexamples;
};