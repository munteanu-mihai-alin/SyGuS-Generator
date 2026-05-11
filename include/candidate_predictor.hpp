#pragma once

#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct SExpr;

class CandidatePredictor {
 public:
  bool loadModel(const std::string& json_path);
  bool isLoaded() const { return loaded_; }

  double score(const SExpr& candidate,
               const std::vector<std::string>& param_names) const;

  bool predict(const SExpr& candidate,
               const std::vector<std::string>& param_names) const;

 private:
  struct SVMModel {
    std::vector<std::string> feature_names;
    std::vector<double> scaler_mean;
    std::vector<double> scaler_scale;
    std::vector<std::vector<double>> support_vectors;
    std::vector<double> dual_coef;
    double intercept = 0.0;
    double gamma = 0.0;
  };

  static std::vector<double> extractFeatures(
      const SExpr& candidate, const std::vector<std::string>& param_names);

  static void collectOperators(const SExpr& node,
                               std::unordered_map<std::string, int>& counts);
  static void collectAtoms(const SExpr& node,
                           std::unordered_set<std::string>& atoms);
  static int astDepth(const SExpr& node);
  static int astNodeCount(const SExpr& node);
  static int letDepth(const SExpr& node);
  static bool isConstant(const std::string& atom);

  double rbfKernel(const std::vector<double>& a,
                   const std::vector<double>& b) const;
  double decisionFunction(const std::vector<double>& scaled_features) const;

  SVMModel model_;
  bool loaded_ = false;

  static const std::vector<std::string>& knownOperators();
};
