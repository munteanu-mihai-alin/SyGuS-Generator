(set-logic AUFLIA )

(set-option :produce-models true)

(declare-const A (Array Int Int))
(declare-const n Int)
(assert (forall ((i Int) (j Int)) (=> (and (>= i 0) (< i n)
                                                (>= j 0) (< j n)
                                                (= (select A i) (select A j)))
                                           (= i j))))
(check-sat)
(get-model)