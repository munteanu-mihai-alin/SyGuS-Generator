//
// Created by munte on 2/14/2025.
//

#ifndef SYGUSPARSER_H
#define SYGUSPARSER_H


#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <sstream>
#include <cctype>

// --------------------------
// AST Data Structures (Updated for BitVec)
// --------------------------
struct GrammarTerm;

struct SynthFun {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params; // (name, type)
    std::string return_type;
    std::map<std::string, std::vector<std::vector<std::shared_ptr<GrammarTerm>>>> grammar;
};

struct DeclareVar {
    std::string name;
    std::string type; // Supports "Int", "(BitVec N)", etc.
};

struct Constraint {
    std::vector<std::string> expr; // S-expression with BV operators
};

struct SyGuSProgram {
    std::string logic;
    std::vector<SynthFun> synth_funs;
    std::vector<DeclareVar> declare_vars;
    std::vector<Constraint> constraints;
};

struct GrammarTerm {
    std::string symbol; // e.g., "bvadd", "Start"
    std::vector<std::shared_ptr<GrammarTerm>> children;

    GrammarTerm(const std::string& sym) : symbol(sym) {}
    GrammarTerm(const std::string& sym, const std::vector<std::shared_ptr<GrammarTerm>>& ch)
        : symbol(sym), children(ch) {}
};
class SyGuSParser {
    std::vector<std::string> tokens;
    size_t pos = 0;
    SyGuSProgram program;

public:
    SyGuSProgram parse(const std::string& input);
private:
    void tokenize(const std::string& input);
    std::string parseAtom() ;
    std::string parseType();
    SynthFun parseSynthFun();
    std::map<std::string, std::vector<std::vector<std::shared_ptr<GrammarTerm>>>> parseGrammar();
    std::shared_ptr<GrammarTerm> parseGrammarTerm() ;
    DeclareVar parseDeclareVar();
    Constraint parseConstraint();
};

#endif //SYGUSPARSER_H
