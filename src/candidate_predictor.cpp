#include "candidate_predictor.hpp"

#include "sygus_ast.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace {
}  // namespace

const std::vector<std::string>& CandidatePredictor::knownOperators() {
  static const std::vector<std::string> ops = {
      "+",          "-",           "*",         "div",
      "mod",        "abs",         "and",       "or",
      "not",        "=>",          "=",         "<",
      ">",          "<=",          ">=",        "ite",
      "let",        "bvand",       "bvor",      "bvxor",
      "bvnot",      "bvneg",       "bvadd",     "bvsub",
      "bvmul",      "bvudiv",      "bvurem",    "bvshl",
      "bvlshr",     "bvashr",      "bvult",     "bvslt",
      "bvule",      "bvsle",       "concat",    "extract",
      "zero_extend", "sign_extend", "str.++",    "str.len",
      "str.at",     "str.substr",  "str.contains", "str.replace",
      "str.indexof", "int.to.str",  "str.to.int",
  };
  return ops;
}

bool CandidatePredictor::loadModel(const std::string& model_path) {
  std::ifstream file(model_path);
  if (!file.is_open()) return false;

  std::string tag;
  size_t count = 0;

  file >> tag >> count;
  if (tag != "features") return false;
  model_.feature_names.resize(count);
  for (size_t i = 0; i < count; ++i) {
    file >> model_.feature_names[i];
  }

  file >> tag >> count;
  if (tag != "scaler_mean") return false;
  model_.scaler_mean.resize(count);
  for (size_t i = 0; i < count; ++i) file >> model_.scaler_mean[i];

  file >> tag >> count;
  if (tag != "scaler_scale") return false;
  model_.scaler_scale.resize(count);
  for (size_t i = 0; i < count; ++i) file >> model_.scaler_scale[i];

  file >> tag >> model_.gamma;
  if (tag != "gamma") return false;

  file >> tag >> model_.intercept;
  if (tag != "intercept") return false;

  size_t n_sv = 0;
  size_t n_feat = 0;
  file >> tag >> n_sv >> n_feat;
  if (tag != "support_vectors") return false;
  model_.support_vectors.resize(n_sv);
  for (size_t i = 0; i < n_sv; ++i) {
    model_.support_vectors[i].resize(n_feat);
    for (size_t j = 0; j < n_feat; ++j) {
      file >> model_.support_vectors[i][j];
    }
  }

  file >> tag >> count;
  if (tag != "dual_coef") return false;
  model_.dual_coef.resize(count);
  for (size_t i = 0; i < count; ++i) file >> model_.dual_coef[i];

  loaded_ = !model_.feature_names.empty() && !model_.support_vectors.empty() &&
            model_.scaler_mean.size() == model_.feature_names.size();
  return loaded_;
}

void CandidatePredictor::collectOperators(
    const SExpr& node, std::unordered_map<std::string, int>& counts) {
  if (node.isList() && !node.list.empty() && node.list[0].isAtom()) {
    counts[node.list[0].atom]++;
    for (size_t i = 1; i < node.list.size(); ++i) {
      collectOperators(node.list[i], counts);
    }
  } else if (node.isList()) {
    for (const auto& child : node.list) {
      collectOperators(child, counts);
    }
  }
}

void CandidatePredictor::collectAtoms(
    const SExpr& node, std::unordered_set<std::string>& atoms) {
  if (node.isAtom()) {
    atoms.insert(node.atom);
  } else {
    for (const auto& child : node.list) {
      collectAtoms(child, atoms);
    }
  }
}

int CandidatePredictor::astDepth(const SExpr& node) {
  if (node.isAtom()) return 1;
  int max_child = 0;
  for (const auto& child : node.list) {
    max_child = std::max(max_child, astDepth(child));
  }
  return 1 + max_child;
}

int CandidatePredictor::astNodeCount(const SExpr& node) {
  if (node.isAtom()) return 1;
  int count = 1;
  for (const auto& child : node.list) {
    count += astNodeCount(child);
  }
  return count;
}

int CandidatePredictor::letDepth(const SExpr& node) {
  if (node.isAtom()) return 0;
  if (!node.list.empty() && node.list[0].isAtom() &&
      node.list[0].atom == "let") {
    int max_child = 0;
    for (size_t i = 1; i < node.list.size(); ++i) {
      max_child = std::max(max_child, letDepth(node.list[i]));
    }
    return 1 + max_child;
  }
  int max_child = 0;
  for (const auto& child : node.list) {
    max_child = std::max(max_child, letDepth(child));
  }
  return max_child;
}

bool CandidatePredictor::isConstant(const std::string& atom) {
  if (atom.empty()) return false;
  if (atom == "true" || atom == "false") return true;
  if (atom[0] == '#') return true;
  if (atom[0] == '"') return true;
  if (std::isdigit(atom[0]) || (atom[0] == '-' && atom.size() > 1))
    return true;
  return false;
}

std::vector<double> CandidatePredictor::extractFeatures(
    const SExpr& candidate, const std::vector<std::string>& param_names) {
  std::unordered_map<std::string, int> op_counts;
  collectOperators(candidate, op_counts);

  std::unordered_set<std::string> atoms;
  collectAtoms(candidate, atoms);

  int constant_count = 0;
  int variable_count = 0;
  for (const auto& a : atoms) {
    if (isConstant(a))
      ++constant_count;
    else if (a != "_")
      ++variable_count;
  }

  double param_coverage = 0.0;
  if (!param_names.empty()) {
    int used = 0;
    for (const auto& p : param_names) {
      if (atoms.count(p)) ++used;
    }
    param_coverage = static_cast<double>(used) / param_names.size();
  }

  std::unordered_map<std::string, double> features;
  features["ast_depth"] = static_cast<double>(astDepth(candidate));
  features["node_count"] = static_cast<double>(astNodeCount(candidate));
  features["let_depth"] = static_cast<double>(letDepth(candidate));
  features["constant_count"] = static_cast<double>(constant_count);
  features["variable_count"] = static_cast<double>(variable_count);
  features["param_coverage"] = param_coverage;
  features["unique_operators"] = static_cast<double>(op_counts.size());

  for (const auto& op : knownOperators()) {
    auto it = op_counts.find(op);
    features["op_" + op] = it != op_counts.end() ? static_cast<double>(it->second) : 0.0;
  }

  std::vector<double> result;
  result.reserve(knownOperators().size() + 7);

  std::vector<std::string> sorted_names;
  for (const auto& [name, _] : features) {
    sorted_names.push_back(name);
  }
  std::sort(sorted_names.begin(), sorted_names.end());

  for (const auto& name : sorted_names) {
    result.push_back(features[name]);
  }

  return result;
}

double CandidatePredictor::rbfKernel(const std::vector<double>& a,
                                     const std::vector<double>& b) const {
  double sq_dist = 0.0;
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
    double diff = a[i] - b[i];
    sq_dist += diff * diff;
  }
  return std::exp(-model_.gamma * sq_dist);
}

double CandidatePredictor::decisionFunction(
    const std::vector<double>& scaled_features) const {
  double result = model_.intercept;
  for (size_t i = 0; i < model_.support_vectors.size(); ++i) {
    result += model_.dual_coef[i] * rbfKernel(scaled_features,
                                              model_.support_vectors[i]);
  }
  return result;
}

double CandidatePredictor::score(
    const SExpr& candidate,
    const std::vector<std::string>& param_names) const {
  if (!loaded_) return 0.0;

  auto raw_features = extractFeatures(candidate, param_names);

  std::vector<double> scaled(raw_features.size());
  for (size_t i = 0; i < raw_features.size() && i < model_.scaler_mean.size();
       ++i) {
    scaled[i] = (raw_features[i] - model_.scaler_mean[i]) /
                (model_.scaler_scale[i] > 1e-10 ? model_.scaler_scale[i] : 1.0);
  }

  return decisionFunction(scaled);
}

bool CandidatePredictor::predict(
    const SExpr& candidate,
    const std::vector<std::string>& param_names) const {
  return score(candidate, param_names) > 0.0;
}
