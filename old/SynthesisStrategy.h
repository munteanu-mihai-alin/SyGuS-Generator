//
// Created by munte on 2/16/2025.
//

#ifndef SYNTHESISSTRATEGY_H
#define SYNTHESISSTRATEGY_H
#pragma once
#include "sygus_parser.hpp"  // Includes SyGuSProgram and GrammarTerm

class SynthesisStrategy {
public:
    virtual ~SynthesisStrategy() = default;
    virtual std::shared_ptr<GrammarTerm> synthesize(
        const SyGuSProgram& program,
        z3::context& ctx,
        const std::vector<z3::expr>& counterexamples
    ) = 0;
};
#endif //SYNTHESISSTRATEGY_H
