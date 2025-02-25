#pragma once
#include "synthesis_strategy.hpp"
#include <random>

class SAStrategy : public SynthesisStrategy {
public:
    SAStrategy(double initial_temp = 1000.0, double cooling_rate = 0.995);
    std::shared_ptr<GrammarTerm> synthesize(
        const SyGuSProgram& program,
        cvc5::TermManager& tm,
        cvc5::Solver& solver,
        std::vector<cvc5::Term>& counterexamples
    ) override;

private:
    double temp;
    double cooling_rate;
    std::mt19937 gen;
    std::uniform_real_distribution<> prob_dist;
    
    std::shared_ptr<GrammarTerm> mutate(
        const std::shared_ptr<GrammarTerm>& term,
        const SyGuSProgram& program
    );
    double evaluateCost(
        const std::shared_ptr<GrammarTerm>& candidate,
        cvc5::TermManager& tm,
        cvc5::Solver& solver,
        const std::vector<cvc5::Term>& counterexamples
    );
};