#include "sygus_synthesizer.hpp"
#include "bfs_strategy.hpp"
#include "sa_strategy.hpp"
#include <cvc5/cvc5.h>
#include <iostream>
#include <fstream>
#include <string>

const std::string getElementAtIndex = R"(
    (set-logic LIA)
    (synth-fun findIdx ((y1 Int) (y2 Int) (k1 Int)) Int
        ((Start Int (0 1 2 y1 y2 k1 (ite BoolExpr Start Start)))
        (BoolExpr Bool ((< Start Start) (<= Start Start) (> Start Start) (>= Start Start)))))
        (declare-var x1 Int)
        (declare-var x2 Int)
        (declare-var k Int)
        (constraint (>=(< x1 x2) (>= (< k x1) (= (findIdx x1 x2 k) 0))))
        (constraint (>= (< x1 x2) (>= (> k x2) (= (findIdx x1 x2 k) 2))))
        (constraint (>= (< x1 x2) (>= (and (> k x1) (< k x2)) (= (findIdx x1 x2 k) 1))))

        (check-synth)
    )";

const std::string maxOf2 = R"(
;; The background theory is linear integer arithmetic
(set-logic LIA)

;; Name and signature of the function to be synthesized
(synth-fun max2 ((x Int) (y Int)) Int

    ;; Declare the non-terminals that would be used in the grammar
    ((I Int) (B Bool))

    ;; Define the grammar for allowed implementations of max2
    ((I Int (x y 0 1
             (+ I I) (- I I)
             (ite B I I)))
     (B Bool ((and B B) (or B B) (not B)
              (= I I) (<= I I) (>= I I))))
)

(declare-var x Int)
(declare-var y Int)

;; Define the semantic constraints on the function
(constraint (>= (max2 x y) x))
(constraint (>= (max2 x y) y))
(constraint (or (= x (max2 x y)) (= y (max2 x y))))

(check-synth)
)";

int main(int argc, char* argv[]) {

    SyGuSParser parser;
    std::string input = getElementAtIndex;
    //std::string input = maxOf2;
    SyGuSProgram program = parser.parse(input);

    /*cvc5::TermManager tm;*/
    cvc5::Solver solver;
    solver.setOption("sygus", "true");
    solver.setOption("produce-models", "true");

    auto strategy = std::make_unique<BFSStrategy>();
    SyGuSSynthesizer synthesizer(std::move(strategy), program,/* tm,*/ solver);
    auto solution = synthesizer.synthesize();

    if (solution) {
        std::cout << "Solution: ";
    } else {
        std::cout << "No solution found.\n";
    }
}