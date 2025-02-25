#include "sygus_synthesizer.hpp"
#include "bfs_strategy.hpp"
#include "sa_strategy.hpp"
#include <cvc5/cvc5.h>
#include <iostream>
#include <cvc5/cvc5.h>
int main() {
    SyGuSParser parser;
    std::string input = R"(
        (set-logic BV)
        (synth-fun f ((x (_ BitVec 8))) (_ BitVec 8)
            ((Start (_ BitVec 8) (x (bvadd Start Start))))
        (constraint (= (f #x02) #x04))
        (check-synth)
    )";
    SyGuSProgram program = parser.parse(input);

    cvc5::TermManager tm;
    cvc5::Solver solver(tm);
    solver.setOption("sygus", "true");
    solver.setOption("produce-models", "true");

    auto strategy = std::make_unique<BFSStrategy>();
    SyGuSSynthesizer synthesizer(std::move(strategy), program, tm, solver);
    auto solution = synthesizer.synthesize();

    if (solution) {
        std::cout << "Solution: ";
        //printTerm(solution);  // Implement this to print the term
    } else {
        std::cout << "No solution found.\n";
    }
    return 0;
}