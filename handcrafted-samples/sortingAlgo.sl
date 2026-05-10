(set-logic LIA) ; Logica folosită: Linear Integer Arithmetic

; Definim tipul de date pentru liste
(declare-datatype List (
  (nil) ; Lista goală
  (cons (head Int) (tail List)) ; Constructor pentru liste
))


; Funcția de sintezat
(synth-fun isSorted ((l List)) Bool ; Funcția ia o listă și returnează un boolean
  ; Gramatica sintactică (spațiul de expresii permise)
  ((Start Bool)(StartInt Int)(StartList List))
  ((Start Bool (
    true
    false
    (and Start Start)
    (or Start Start)
    (not Start)
    (<= StartInt StartInt) ; Comparații între numere întregi
  ))
  (StartInt Int (
    0
    1
    (head StartList) ; Accesează capul unei liste
  ))
  (StartList List (
    l ; Lista de intrare
    (tail StartList) ; Accesează coada unei liste
  )))
)

; Specificația logică (ce trebuie să facă funcția)
(declare-var l List)

; Cazul de bază: lista goală este sortată
(constraint (isSorted nil))

; Cazul recursiv: o listă este sortată dacă:
; 1. Capul este mai mic sau egal decât capul cozii.
; 2. Coada este sortată.
(constraint (forall ((x Int) (xs List))
  (=> (isSorted (cons x xs))
      (and (<= x (head xs)) (isSorted xs))
  )
))

; Exemplu de test: [1, 2, 3] este sortată
;(constraint (isSorted (cons 1 (cons 2 (cons 3 nil)))))
(constraint (isSorted (cons 1 (cons 2  nil))))
; Exemplu de test: [3, 2, 1] nu este sortată
(constraint (not (isSorted (cons 3 (cons 2  nil)))))

(check-synth)

