; Define list type (nil and cons)
(declare-datatype List (
  (nil)
  (cons (head Int) (tail List))
))

; Synthesize merge sort
(synth-fun mergeSort ((l List)) List(
  ((Start List))
  ; Grammar for mergeSort and helper functions
  ((Start List (
    nil                                      ; Empty list
    (cons (Int) (List))                      ; Construct a list
    (merge (List) (List))                    ; Merge two sorted lists
    (mergeSort (split1 (List)))              ; Recursive call on first split
    (mergeSort (split2 (List)))              ; Recursive call on second split
  ))
   (List (
    l                                        ; Input list
    (split1 (List))                          ; Split into first half
    (split2 (List))                          ; Split into second half
   ))
   (split1 (List) (
    (match List (
      (nil nil)
      ((cons x xs) (cons x (split2 xs)))     ; Split logic
    ))
   ))
   (split2 (List) (
    (match List (
      (nil nil)
      ((cons x xs) (split1 xs))              ; Alternate split
    ))
   ))
   (merge (List (List)) (
    (match List (
      (nil (Start))                          ; Merge with empty list
      ((cons x xs)
        (match (List) (
          (nil (cons x xs))                  ; Merge non-empty lists
          ((cons y ys)
            (ite (<= x y)
              (cons x (merge xs (cons y ys)))
              (cons y (merge (cons x xs) ys))
            )
          )
        ))
      )
    ))
   ))
  )
)

; Specification: Output must be sorted and a permutation of input
(define-fun is-sorted ((l List)) Bool
  (match l
    (nil true)
    ((cons x xs)
      (match xs
        (nil true)
        ((cons y ys) (and (<= x y) (is-sorted xs)))
      )
    )
  )
)

(define-fun is-permutation ((l1 List) (l2 List)) Bool
  (and
    (forall ((i Int)) (= (count i l1) (count i l2))) ; Same element counts
    (is-sorted l2)                                   ; Output is sorted
  )
)

; Constraint: mergeSort must return a sorted permutation
(constraint
  (forall ((l List))
    (is-permutation l (mergeSort l))
  )
)

(check-synth)