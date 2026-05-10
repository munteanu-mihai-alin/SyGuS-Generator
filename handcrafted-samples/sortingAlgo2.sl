(set-logic LIA) ; Use linear integer arithmetic

(declare-datatype List (
  (nil)
  (cons (head Int) (tail List))
))

;; Define the input and output types
(synth-fun sort ((x (List Int))) (List Int)
  ;; Grammar for the sort function
  ((Start (List Int) (
    (nil) ; Empty list
    (insert (Int) (Start)) ; Insert an integer into a sorted list
    (ite (Bool) (Start) (Start)) ; If-then-else
  ))
   (Bool (
    (<= (Int) (Int)) ; Less than or equal to
    (>= (Int) (Int)) ; Greater than or equal to
    (and (Bool) (Bool)) ; Logical AND
    (or (Bool) (Bool)) ; Logical OR
    (not (Bool)) ; Logical NOT
   ))
   (Int (
    (head (List Int)) ; First element of the list
    (0) ; Constant 0
    (1) ; Constant 1
   ))
  )
)

;; Define the specification for sorting
(define-fun is-sorted ((x (List Int))) Bool
  (match x
    (nil true) ; Empty list is sorted
    ((cons y nil) true) ; Single-element list is sorted
    ((cons y (cons z xs)) (and (<= y z) (is-sorted (cons z xs)))) ; Recursive check
  )
)

(define-fun is-permutation ((x (List Int)) (y (List Int))) Bool
  (and
    (forall ((i Int)) (= (count i x) (count i y))) ; Count of each element is the same
    (is-sorted y) ; Output is sorted
  )
)

;; Synthesize the sort function
(constraint
  (forall ((x (List Int)))
    (is-permutation x (sort x))
  )
)

(check-synth)