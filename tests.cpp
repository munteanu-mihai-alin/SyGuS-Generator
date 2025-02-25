//
// Created by munte on 2/14/2025.
//
#include <string>
#include <iostream>
std::string s = R"()";

std::string input = R"(
    (set-logic BV)
    (synth-fun f ((x (_ BitVec 8))) ((_ BitVec 8))
        ((Start ((_ BitVec 8)) (x (bvadd Start Start) (bvmul Start Start)))
    (declare-var y (_ BitVec 8))
    (constraint (= (f y) (bvadd y y)))
    )";


int main() {
    std::string input = R"(
    (set-logic BV)
    (synth-fun f ((x (_ BitVec 8))) ((_ BitVec 8))
        ((Start ((_ BitVec 8)) (x (bvadd Start Start) (bvmul Start Start)))
    (declare-var y (_ BitVec 8))
    (constraint (= (f y) (bvadd y y)))
    )";

    SyGuSParser parser;
    SyGuSProgram program = parser.parse(input);

    std::cout << "Logic: " << program.logic << "\n";
    for(const auto& sf : program.synth_funs) {
        std::cout << "Function: " << sf.name
                  << ", Return: " << sf.return_type << "\n";
        std::cout << "Grammar:\n";
        for(const auto& [nt, prods] : sf.grammar) {
            std::cout << "  " << nt << " -> ";
            for(const auto& prod : prods) {
                for(const auto& term : prod) {
                    std::cout << term->symbol << " ";
                }
                std::cout << " | ";
            }
            std::cout << "\n";
        }
    }
    for(const auto& dv : program.declare_vars) {
        std::cout << "Declared var: " << dv.name << " : " << dv.type << "\n";
    }
    for(const auto& c : program.constraints) {
        std::cout << "Constraint: ";
        for(const auto& e : c.expr) std::cout << e << " ";
        std::cout << "\n";
    }

    return 0;
