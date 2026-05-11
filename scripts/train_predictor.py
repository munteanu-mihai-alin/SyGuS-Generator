"""Train an SVM predictor to filter SyGuS candidates.

Features are extracted from S-expression ASTs:
  - AST depth and node count
  - Operator frequency vector (ite, +, -, *, and, or, not, bvand, ...)
  - Variable coverage ratio (how many declared params appear)
  - Constant count
  - let-binding depth

Usage:
    python scripts/train_predictor.py training-data.jsonl \
        --output model.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


KNOWN_OPERATORS = [
    "+", "-", "*", "div", "mod", "abs",
    "and", "or", "not", "=>",
    "=", "<", ">", "<=", ">=",
    "ite",
    "let",
    "bvand", "bvor", "bvxor", "bvnot", "bvneg",
    "bvadd", "bvsub", "bvmul", "bvudiv", "bvurem",
    "bvshl", "bvlshr", "bvashr",
    "bvult", "bvslt", "bvule", "bvsle",
    "concat", "extract", "zero_extend", "sign_extend",
    "str.++", "str.len", "str.at", "str.substr",
    "str.contains", "str.replace", "str.indexof",
    "int.to.str", "str.to.int",
]


def tokenize_sexpr(text: str) -> list[str]:
    tokens: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in " \t\n\r":
            i += 1
        elif c == "(":
            tokens.append("(")
            i += 1
        elif c == ")":
            tokens.append(")")
            i += 1
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                if text[j] == '\\':
                    j += 1
                j += 1
            tokens.append(text[i:j + 1])
            i = j + 1
        elif c == '#':
            j = i + 1
            while j < n and text[j] not in " \t\n\r()":
                j += 1
            tokens.append(text[i:j])
            i = j
        else:
            j = i + 1
            while j < n and text[j] not in " \t\n\r()":
                j += 1
            tokens.append(text[i:j])
            i = j
    return tokens


@dataclass
class ASTNode:
    value: str
    children: list[ASTNode] = field(default_factory=list)


def parse_sexpr(tokens: list[str], pos: int = 0) -> tuple[ASTNode, int]:
    if pos >= len(tokens):
        return ASTNode(""), pos

    if tokens[pos] == "(":
        pos += 1
        children: list[ASTNode] = []
        while pos < len(tokens) and tokens[pos] != ")":
            child, pos = parse_sexpr(tokens, pos)
            children.append(child)
        if pos < len(tokens):
            pos += 1
        if children:
            head = children[0]
            head.children = children[1:]
            return head, pos
        return ASTNode(""), pos
    else:
        return ASTNode(tokens[pos]), pos + 1


def ast_depth(node: ASTNode) -> int:
    if not node.children:
        return 1
    return 1 + max(ast_depth(c) for c in node.children)


def ast_node_count(node: ASTNode) -> int:
    return 1 + sum(ast_node_count(c) for c in node.children)


def collect_operators(node: ASTNode, counts: dict[str, int]) -> None:
    if node.children:
        counts[node.value] = counts.get(node.value, 0) + 1
        for child in node.children:
            collect_operators(child, counts)


def collect_atoms(node: ASTNode, atoms: set[str]) -> None:
    if not node.children:
        atoms.add(node.value)
    else:
        for child in node.children:
            collect_atoms(child, atoms)


def let_depth(node: ASTNode) -> int:
    if node.value == "let":
        child_depths = [let_depth(c) for c in node.children]
        return 1 + (max(child_depths) if child_depths else 0)
    if not node.children:
        return 0
    return max(let_depth(c) for c in node.children)


RE_INT_LITERAL = re.compile(r"^-?\d+$")
RE_HEX_LITERAL = re.compile(r"^#x[0-9a-fA-F]+$")
RE_BIN_LITERAL = re.compile(r"^#b[01]+$")
RE_STRING_LITERAL = re.compile(r'^".*"$')


def is_constant(atom: str) -> bool:
    return bool(
        RE_INT_LITERAL.match(atom)
        or RE_HEX_LITERAL.match(atom)
        or RE_BIN_LITERAL.match(atom)
        or RE_STRING_LITERAL.match(atom)
        or atom in ("true", "false")
    )


def extract_params_from_define_fun(text: str) -> list[str]:
    tokens = tokenize_sexpr(text)
    if len(tokens) < 4 or tokens[0] != "(":
        return []
    try:
        i = tokens.index("define-fun")
    except ValueError:
        return []
    i += 2
    if i >= len(tokens) or tokens[i] != "(":
        return []
    i += 1
    params: list[str] = []
    depth = 1
    while i < len(tokens) and depth > 0:
        if tokens[i] == "(":
            depth += 1
            i += 1
            if i < len(tokens) and tokens[i] not in ("(", ")"):
                params.append(tokens[i])
        elif tokens[i] == ")":
            depth -= 1
        i += 1
    return params


def extract_body_from_define_fun(text: str) -> str:
    tokens = tokenize_sexpr(text)
    try:
        df_idx = tokens.index("define-fun")
    except ValueError:
        return text

    pos = df_idx + 1
    if pos >= len(tokens):
        return text
    pos += 1

    if pos < len(tokens) and tokens[pos] == "(":
        depth = 1
        pos += 1
        while pos < len(tokens) and depth > 0:
            if tokens[pos] == "(":
                depth += 1
            elif tokens[pos] == ")":
                depth -= 1
            pos += 1

    if pos < len(tokens) and tokens[pos] == "(":
        depth = 1
        pos += 1
        while pos < len(tokens) and depth > 0:
            if tokens[pos] == "(":
                depth += 1
            elif tokens[pos] == ")":
                depth -= 1
            pos += 1
    else:
        pos += 1

    body_tokens = tokens[pos:]
    if body_tokens and body_tokens[-1] == ")":
        body_tokens = body_tokens[:-1]

    return " ".join(body_tokens)


def extract_features(candidate: str) -> dict[str, float]:
    if candidate.strip().startswith("(define-fun "):
        params = extract_params_from_define_fun(candidate)
        body = extract_body_from_define_fun(candidate)
    else:
        params = []
        body = candidate

    tokens = tokenize_sexpr(body)
    if not tokens:
        return _zero_features()

    ast, _ = parse_sexpr(tokens)

    op_counts: dict[str, int] = {}
    collect_operators(ast, op_counts)

    atoms: set[str] = set()
    collect_atoms(ast, atoms)

    constants = [a for a in atoms if is_constant(a)]
    variables = [a for a in atoms if not is_constant(a) and a not in ("_",)]

    param_coverage = 0.0
    if params:
        used = sum(1 for p in params if p in atoms)
        param_coverage = used / len(params)

    features: dict[str, float] = {
        "ast_depth": float(ast_depth(ast)),
        "node_count": float(ast_node_count(ast)),
        "let_depth": float(let_depth(ast)),
        "constant_count": float(len(constants)),
        "variable_count": float(len(variables)),
        "param_coverage": param_coverage,
        "unique_operators": float(len(op_counts)),
    }

    for op in KNOWN_OPERATORS:
        features[f"op_{op}"] = float(op_counts.get(op, 0))

    return features


def _zero_features() -> dict[str, float]:
    features: dict[str, float] = {
        "ast_depth": 0.0,
        "node_count": 0.0,
        "let_depth": 0.0,
        "constant_count": 0.0,
        "variable_count": 0.0,
        "param_coverage": 0.0,
        "unique_operators": 0.0,
    }
    for op in KNOWN_OPERATORS:
        features[f"op_{op}"] = 0.0
    return features


def load_training_data(path: Path) -> list[dict[str, Any]]:
    samples = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                samples.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return samples


def main() -> None:
    parser = argparse.ArgumentParser(description="Train SVM predictor for SyGuS candidates")
    parser.add_argument("training_data", type=Path)
    parser.add_argument("--output", "-o", type=Path, default=Path("model.json"))
    parser.add_argument("--test-split", type=float, default=0.2)
    parser.add_argument("--features-only", action="store_true",
                        help="Just extract features and print stats, don't train")
    args = parser.parse_args()

    samples = load_training_data(args.training_data)
    if not samples:
        print("No training data found.", file=sys.stderr)
        return

    print(f"Loaded {len(samples)} samples")
    pos = sum(1 for s in samples if s["label"] == 1)
    neg = sum(1 for s in samples if s["label"] == 0)
    print(f"  Positive: {pos}, Negative: {neg}")

    feature_names: list[str] = []
    feature_matrix: list[list[float]] = []
    labels: list[int] = []

    for i, sample in enumerate(samples):
        features = extract_features(sample["candidate"])
        if i == 0:
            feature_names = sorted(features.keys())

        row = [features[name] for name in feature_names]
        feature_matrix.append(row)
        labels.append(sample["label"])

        if i < 3:
            print(f"\nSample {i} (label={sample['label']}):")
            for name in feature_names[:10]:
                print(f"  {name}: {features[name]}")

    if args.features_only:
        print(f"\nFeature count: {len(feature_names)}")
        print(f"Feature names: {feature_names}")
        return

    try:
        import numpy as np
        from sklearn.model_selection import train_test_split
        from sklearn.preprocessing import StandardScaler
        from sklearn.svm import SVC
        from sklearn.metrics import classification_report, accuracy_score
    except ImportError:
        print("\nInstall scikit-learn: pip install scikit-learn numpy", file=sys.stderr)
        return

    X = np.array(feature_matrix)
    y = np.array(labels)

    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=args.test_split, random_state=42, stratify=y
    )

    scaler = StandardScaler()
    X_train_scaled = scaler.fit_transform(X_train)
    X_test_scaled = scaler.transform(X_test)

    svm = SVC(kernel="rbf", C=1.0, gamma="scale", class_weight="balanced")
    svm.fit(X_train_scaled, y_train)

    y_pred = svm.predict(X_test_scaled)
    print(f"\nAccuracy: {accuracy_score(y_test, y_pred):.3f}")
    print(classification_report(y_test, y_pred, target_names=["rejected", "solution"]))

    model_data = {
        "feature_names": feature_names,
        "scaler_mean": scaler.mean_.tolist(),
        "scaler_scale": scaler.scale_.tolist(),
        "support_vectors": svm.support_vectors_.tolist(),
        "dual_coef": svm.dual_coef_.tolist(),
        "intercept": svm.intercept_.tolist(),
        "gamma": float(svm._gamma),
        "classes": svm.classes_.tolist(),
        "n_support": svm.n_support_.tolist(),
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(model_data, indent=2), encoding="utf-8")

    flat_path = args.output.with_suffix(".txt")
    with open(flat_path, "w", encoding="utf-8") as f:
        f.write(f"features {len(feature_names)}\n")
        for name in feature_names:
            f.write(f"{name}\n")
        f.write(f"scaler_mean {len(scaler.mean_)}\n")
        for v in scaler.mean_:
            f.write(f"{v:.15e}\n")
        f.write(f"scaler_scale {len(scaler.scale_)}\n")
        for v in scaler.scale_:
            f.write(f"{v:.15e}\n")
        f.write(f"gamma {svm._gamma:.15e}\n")
        f.write(f"intercept {svm.intercept_[0]:.15e}\n")
        n_sv = svm.support_vectors_.shape[0]
        n_feat = svm.support_vectors_.shape[1]
        f.write(f"support_vectors {n_sv} {n_feat}\n")
        for row in svm.support_vectors_:
            f.write(" ".join(f"{v:.15e}" for v in row) + "\n")
        f.write(f"dual_coef {len(svm.dual_coef_[0])}\n")
        for v in svm.dual_coef_[0]:
            f.write(f"{v:.15e}\n")

    print(f"\nModel saved to {args.output} and {flat_path}")


if __name__ == "__main__":
    main()
