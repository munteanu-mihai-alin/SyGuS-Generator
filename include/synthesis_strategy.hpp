#pragma once
#include "sygus_parser.hpp"
#include <cvc5/cvc5.h>

class SynthesisStrategy {
public:
    virtual ~SynthesisStrategy() = default;
    virtual std::shared_ptr<GrammarTerm> synthesize(
        const SyGuSProgram& program,
        cvc5::TermManager& tm,
        cvc5::Solver& solver,
        std::vector<cvc5::Term>& counterexamples
    ) = 0;

protected:
    /*const std::shared_ptr<GrammarTerm>& candidate;
    const SyGuSProgram& program;
    cvc5::TermManager& tm;
    cvc5::Solver& solver;
    std::vector<cvc5::Term>& counterexamples;*/

    cvc5::Term termToCVC5(
        const std::shared_ptr<GrammarTerm>& term,
        cvc5::TermManager& tm,
        cvc5::Solver& solver,
        const std::map<std::string, cvc5::Term>& var_map
    );
    
    bool verifyCandidate(
        const std::shared_ptr<GrammarTerm>& candidate,
        const SyGuSProgram& program,
        cvc5::TermManager& tm,
        cvc5::Solver& solver,
        std::vector<cvc5::Term>& counterexamples);

    bool isNonTerminal(const std::string& sym, const SyGuSProgram& program) ;

    std::shared_ptr<GrammarTerm> buildFromProduction(
        const std::vector<std::shared_ptr<GrammarTerm>>& prod);

    static cvc5::Term parseConstraintToCVC5(
        const std::vector<std::string>& constraint_tokens,
        cvc5::TermManager& tm,
        cvc5::Solver& solver,
        const std::map<std::string, cvc5::Term>& var_map
    );

private:
    static cvc5::Term parseTerm(
        const std::vector<std::string>& tokens,
        size_t& pos,
        cvc5::TermManager& tm,
        cvc5::Solver& solver,
        const std::map<std::string, cvc5::Term>& var_map
    );

    static cvc5::Kind getKind(const std::string& op);


};