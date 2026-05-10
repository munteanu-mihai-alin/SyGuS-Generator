(set-logic LIA)
 (synth-inv inv-f ((i Int) (j Int) (i0 Int) (j0 Int)))
 (declare-var i0 Int)
 (declare-var j0 Int)
 (declare-var i Int)
 (declare-var j Int)
 (define-fun pre-f ((i Int) (j Int) (i0 Int) (j0 Int)) Bool
 (and (>= i 0) (and (= i i0) (= j j0))))
 (define-fun trans-f ((i Int) (j Int) (i0 Int) (j0 Int)
 (i! Int) (j! Int) (i0! Int) (j0! Int)) Bool
 (and (and (= i! (- i 1)) (= j! (+ j 1)))
 (and (= i0! i0) (= j0! j0))))
 (define-fun post-f ((i Int) (j Int) (i0 Int) (j0 Int)) Bool
 (= j (+ j0 i0)))
 (inv-constraint inv-f pre-f trans-f post-f)
 (check-synth)