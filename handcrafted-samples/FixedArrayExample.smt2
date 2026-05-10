(set-logic AUFLIA )

(set-option :produce-models true)

(declare-const a (Array Int Int))
(assert (= (select a 0) 5))
(assert (= (select a 1) 4))
(assert (= (select a 2) 3))
(assert (= (select a 3) 2))
(assert (= (select a 4) 1))

(declare-const n Int)
(assert (forall ((i Int) ) (=> (and (>= i 0) (< i n)
                                                (= (select a i) (select a i + 1)))
                                           (> i i + 1))))
(check-sat)
(get-model)
