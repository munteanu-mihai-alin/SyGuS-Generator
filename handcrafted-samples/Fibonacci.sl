(set-logic DTLIA)

(synth-fun f ( (s Int) ) Int

;; Declare the non-terminals that would be used in the grammar
    ((I Int) (B Bool))

    ;; Define the grammar for allowed implementations of max2
    ((I Int ( s 0 1 
            (- I 1) (- I 2)
            )))
)


(declare-var x Int)

(constraint( or ( or ( = ( f 0) 0) ( = ( f 1) 1) )  (= ( f x )  (+ (f( - x 1)) (f(- x 2)) ) )  ))

(check-synth)