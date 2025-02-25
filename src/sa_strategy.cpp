#include "sa_strategy.hpp"
#include <cmath>
#include <algorithm>

using namespace cvc5;

SAStrategy::SAStrategy(double initial_temp, double cooling_rate)
    : temp(initial_temp), cooling_rate(cooling_rate),
      gen(std::random_device{}()), prob_dist(0.0, 1.0) {}

std::shared_ptr<GrammarTerm> SAStrategy::mutate(
    const std::shared_ptr<GrammarTerm>& term,
    const SyGuSProgram& program
) {
    std::vector<std::shared_ptr<GrammarTerm>> nodes;
    std::function<void(const std::shared_ptr<GrammarTerm>&)> collect =
        [&](const std::shared_ptr<GrammarTerm>& node) {
            if (isNonTerminal(node->symbol, program)) nodes.push_back(node);
            for (const auto& child : node->children) collect(child);
        };
    collect(term);

    if (nodes.empty()) return term;
    std::uniform_int_distribution<size_t> dist(0, nodes.size()-1);
    auto target = nodes[dist(gen)];
    const auto& prods = program.synth_funs[0].grammar.at(target->symbol);
    auto new_sub = buildFromProduction(prods[dist(gen) % prods.size()]);

    auto new_term = std::make_shared<GrammarTerm>(*term);
    std::replace(new_term->children.begin(), new_term->children.end(), target, new_sub);
    return new_term;
}

double SAStrategy::evaluateCost(
    const std::shared_ptr<GrammarTerm>& candidate,
    cvc5::TermManager& tm,
    cvc5::Solver& solver,
    const std::vector<cvc5::Term>& counterexamples
) {
    double cost = 0;
    for (const auto& ce : counterexamples) {
        solver.resetAssertions();
        auto term = SynthesisStrategy::termToCVC5(candidate, tm, solver, {});
        solver.assertFormula(solver.mkTerm(cvc5::Kind::EQUAL, {term, ce}));
        if (solver.checkSat().isUnsat()) cost += 1;
    }
    return cost;
}

std::shared_ptr<GrammarTerm> SAStrategy::synthesize(
    const SyGuSProgram& program,
    cvc5::TermManager& tm,
    cvc5::Solver& solver,
    std::vector<cvc5::Term>& counterexamples
) {
    std::uniform_int_distribution<size_t> prod_dist;
    const auto& prods = program.synth_funs[0].grammar.at("Start");
    auto current = buildFromProduction(prods[prod_dist(gen) % prods.size()]);
    auto best = current;
    double current_cost = evaluateCost(current, tm, solver, counterexamples);
    double best_cost = current_cost;

    for (int i = 0; i < 1000 && temp > 1e-6; ++i) {
        auto neighbor = mutate(current, program);
        double neighbor_cost = evaluateCost(neighbor, tm, solver, counterexamples);

        if (neighbor_cost < current_cost ||
            prob_dist(gen) < std::exp((current_cost - neighbor_cost)/temp)) {
            current = neighbor;
            current_cost = neighbor_cost;
        }

        if (neighbor_cost < best_cost) {
            best = neighbor;
            best_cost = neighbor_cost;
        }

        temp *= cooling_rate;
    }
    return best_cost == 0 ? best : nullptr;
}