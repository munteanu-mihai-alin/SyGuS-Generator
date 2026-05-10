(set-logic LIA)
(synth-fun returnMax ( (x1 Int) (x2 Int)) Int)
(declare-var x1 Int)
(declare-var x2 Int)

(constraint (>= (returnMax x1 x2) x1))
(constraint (>= (returnMax x1 x2) x2))

(constraint (or (= x1 (returnMax x1 x2))
				(= x2 (returnMax x1 x2))))
(check-synth)
