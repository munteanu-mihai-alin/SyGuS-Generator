//
// Created by munte on 2/14/2025.
//

#include "sygus_parser.hpp"

SyGuSProgram SyGuSParser::parse(const std::string& input) {
        tokenize(input);
        pos = 0;
        program = SyGuSProgram();

        while(pos < tokens.size()) {
            if(tokens[pos] == "(") {
                pos++;
                if(pos >= tokens.size()) break;

                const std::string cmd = tokens[pos++];
                if(cmd == "set-logic") {
                    program.logic = parseAtom();
                } else if(cmd == "synth-fun") {
                    program.synth_funs.push_back(parseSynthFun());
                } else if(cmd == "declare-var") {
                    program.declare_vars.push_back(parseDeclareVar());
                } else if(cmd == "constraint") {
                    program.constraints.push_back(parseConstraint());
                }

                // Skip to closing )
                while(pos < tokens.size() && tokens[pos] != ")") pos++;
                if(pos < tokens.size()) pos++;
            } else {
                pos++;
            }
        }
        return program;
    }


    // Tokenize input (unchanged, but handles BitVec syntax)
    void SyGuSParser::tokenize(const std::string& input) {
        tokens.clear();
        std::string current;
        bool in_quotes = false;

        for(char c : input) {
            if(c == '"') {
                in_quotes = !in_quotes;
                if(!in_quotes && !current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                continue;
            }

            if(in_quotes) {
                current += c;
            } else {
                if(std::isspace(c) || c == '(' || c == ')') {
                    if(!current.empty()) {
                        tokens.push_back(current);
                        current.clear();
                    }
                    if(c == '(' || c == ')') {
                        tokens.push_back(std::string(1, c));
                    }
                } else {
                    current += c;
                }
            }
        }
    }

    std::string SyGuSParser::parseAtom() {
        return tokens[pos++];
    }

    // parse composite types like BitVec 8
    std::string SyGuSParser::parseType() {
        if (tokens[pos] == "(") {
            pos++;
            std::string type;

            // Recursively parse nested components
            while (pos < tokens.size() && tokens[pos] != ")") {
                if (tokens[pos] == "(") {
                    type += "(" + parseType() + ")";
                } else {
                    type += tokens[pos] + " ";
                    pos++;
                }
            }

            if (pos >= tokens.size() || tokens[pos] != ")") {
                throw std::runtime_error("Mismatched parentheses in type");
            }
            pos++; // Skip closing ')'

            // Remove trailing space and return
            if (!type.empty() && type.back() == ' ') {
                type.pop_back();
            }
            return type;
        } else {
            std::string atom = tokens[pos++];
            return atom;
        }
    }

    SynthFun SyGuSParser::parseSynthFun() {
        SynthFun sf;
        sf.name = parseAtom();

        // Parse parameters with BitVec support
        if(tokens[pos++] != "(") throw std::runtime_error("Expected ( for parameters");
        while(tokens[pos] != ")") {
            if(tokens[pos++] != "(") throw std::runtime_error("Expected ( for param");
            std::string name = parseAtom();
            std::string type = parseType(); // Updated for BitVec
            sf.params.emplace_back(name, type);
            if(tokens[pos++] != ")") throw std::runtime_error("Expected ) for param");
        }
        pos++; // Skip )

        // Parse return type (supports BitVec)
        sf.return_type = parseType();

        // Parse grammar
        sf.grammar = parseGrammar();
        return sf;
    }

    std::map<std::string, std::vector<std::vector<std::shared_ptr<GrammarTerm>>>> SyGuSParser::parseGrammar() {
        std::map<std::string, std::vector<std::vector<std::shared_ptr<GrammarTerm>>>> grammar;
        if(tokens[pos++] != "(") throw std::runtime_error("Expected ( for grammar");

        while(pos < tokens.size() && tokens[pos] == "(") {
            pos++; // Skip (
            std::string nt = parseAtom();
            std::string type = parseType(); // Updated for BitVec

            std::vector<std::vector<std::shared_ptr<GrammarTerm>>> productions;
            while(pos < tokens.size() && tokens[pos] == "(") {
                pos++; // Skip (
                std::vector<std::shared_ptr<GrammarTerm>> production;
                while(pos < tokens.size() && tokens[pos] != ")") {
                    if(tokens[pos] == "(") {
                        pos++;
                        production.push_back(parseGrammarTerm());
                        if(tokens[pos++] != ")") throw std::runtime_error("Expected ) for term");
                    } else {
                        production.push_back(std::make_shared<GrammarTerm>(parseAtom()));
                    }
                }
                pos++; // Skip )
                productions.push_back(production);
            }
            grammar[nt] = productions;
            if(tokens[pos++] != ")") throw std::runtime_error("Expected ) for NT");
        }
        return grammar;
    }

/*
std::map<std::string, std::vector<std::vector<std::shared_ptr<GrammarTerm>>>> SyGuSParser::parseGrammar() {
    std::map<std::string, std::vector<std::vector<std::shared_ptr<GrammarTerm>>>> grammar;
    if (tokens[pos++] != "(") throw std::runtime_error("Expected ( for grammar");

    while (pos < tokens.size() && tokens[pos] == "(") {
        pos++; // Skip '('
        std::string nt = parseAtom();
        std::string type = parseType();

        std::vector<std::vector<std::shared_ptr<GrammarTerm>>> productions;
        while (pos < tokens.size() && tokens[pos] == "(") {
            pos++; // Skip '('
            std::vector<std::shared_ptr<GrammarTerm>> production;
            while (pos < tokens.size() && tokens[pos] != ")") {
                if (tokens[pos] == "(") {
                    pos++; // Skip '('
                    production.push_back(parseGrammarTerm());
                    pos++; // Skip ')'
                } else {
                    production.push_back(std::make_shared<GrammarTerm>(tokens[pos]));
                    pos++;
                }
            }
            pos++; // Skip ')'
            productions.push_back(production);
        }
        grammar[nt] = productions;
        pos++; // Skip ')'
    }
    return grammar;
}
*/

// Helper: Parse a nested grammar term (e.g., (bvadd Start Start))
std::shared_ptr<GrammarTerm> SyGuSParser::parseGrammarTerm() {
    std::string symbol = parseAtom();
    std::vector<std::shared_ptr<GrammarTerm>> children;

    while (pos < tokens.size() && tokens[pos] != ")") {
        if (tokens[pos] == "(") {
            pos++; // Skip '('
            children.push_back(parseGrammarTerm());
            pos++; // Skip ')'
        } else {
            children.push_back(std::make_shared<GrammarTerm>(tokens[pos]));
            pos++;
        }
    }
    return std::make_shared<GrammarTerm>(symbol, children);
}
    DeclareVar SyGuSParser::parseDeclareVar() {
        DeclareVar dv;
        dv.name = parseAtom();
        dv.type = parseType(); // Updated for BitVec
        return dv;
    }

    Constraint SyGuSParser::parseConstraint() {
        Constraint c;
        while(pos < tokens.size() && tokens[pos] != ")") {
            c.expr.push_back(tokens[pos++]);
        }
        return c;
    }
