//
// Created by munte on 2/14/2025.
//

#ifndef SYGUSSYNTHESIZER_H
#define SYGUSSYNTHESIZER_H

#include "SyGuSParser.h"

class SyGuSSynthesizer {
private:
    SyGuSProgram program;  // Parsed SyGuS program
    context& z3_ctx;       // Z3 context

public:
    SyGuSSynthesizer(const SyGuSProgram& prog, context& ctx)
        : program(prog), z3_ctx(ctx) {}

    // Generate a candidate program from the grammar
    std::shared_ptr<GrammarTerm> generateCandidate();

    // Verify the candidate against constraints using Z3
    bool verifyCandidate(const std::shared_ptr<GrammarTerm>& candidate,
                         model& model_out) ;

    // CEGIS loop
    std::shared_ptr<GrammarTerm> synthesize();

    void SyGuSSynthesizer::expandTerm(const std::shared_ptr<GrammarTerm>& term,
                std::vector<std::shared_ptr<GrammarTerm>>& new_terms);

    // Build a GrammarTerm tree from a production rule (list of terms)
    std::shared_ptr<GrammarTerm> SyGuSSynthesizer::buildTermFromProduction(
    const std::vector<std::shared_ptr<GrammarTerm>>& production);
};


#endif //SYGUSSYNTHESIZER_H
