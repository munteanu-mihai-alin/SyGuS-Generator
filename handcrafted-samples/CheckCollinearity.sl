(set-logic LIA)

(synth-fun gcd ((x Int) (y Int)) Int
  ; More constrained grammar based on Euclidean algorithm
  ((Start Int (x y
              (ite BoolExpr Start Start)))
   (BoolExpr Bool ((= y 0)
                  (and BoolExpr BoolExpr)))))

; GCD properties
(constraint (forall ((x Int) (y Int))
  (=> (and (>= x 0) (>= y 0))
      (let ((g (gcd x y)))
        (and
          (= (mod x g) 0)
          (= (mod y g) 0)
          (forall ((d Int))
            (=> (and (> d g) (>= d 0))
                (or (not (= (mod x d) 0))
                    (not (= (mod y d) 0)))))))))

; Base cases
(constraint (forall ((x Int)) (=> (>= x 0) (= (gcd x 0) x))))
(constraint (forall ((y Int)) (=> (>= y 0) (= (gcd 0 y) y))))

; Euclidean algorithm property
(constraint (forall ((x Int) (y Int))
  (=> (and (> y 0) (>= x 0))
      (= (gcd x y) (gcd y (mod x y))))))

; Test cases
(constraint (= (gcd 12 18) 6))
(constraint (= (gcd 17 13) 1))
(constraint (= (gcd 42 56) 14))

(check-synth)