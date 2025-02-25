#include "sygus_synthesizer.hpp"
#include <cvc5/cvc5.h>

SyGuSSynthesizer::SyGuSSynthesizer(
    std::unique_ptr<SynthesisStrategy> strategy,
    const SyGuSProgram& program,
    cvc5::TermManager& tm,
    cvc5::Solver& solver
) : strategy(std::move(strategy)), program(program), tm(tm), solver(solver) {}

std::shared_ptr<GrammarTerm> SyGuSSynthesizer::synthesize() {
    return strategy->synthesize(program, tm, solver, counterexamples);
}