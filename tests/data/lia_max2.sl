(set-logic LIA)

(synth-fun max2 ((x Int) (y Int)) Int
  ((I Int (x y 0 1 (+ I I) (- I I) (ite B I I)))
   (B Bool ((and B B) (or B B) (not B) (= I I) (<= I I) (>= I I)))))

(declare-var x Int)
(declare-var y Int)

(constraint (>= (max2 x y) x))
(constraint (>= (max2 x y) y))
(constraint (or (= x (max2 x y)) (= y (max2 x y))))

(check-synth)
