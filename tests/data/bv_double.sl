(set-logic BV)

(synth-fun f ((x (_ BitVec 8))) (_ BitVec 8)
  ((Start (_ BitVec 8) (x #x00 #x01 (bvadd Start Start) (bvmul Start Start)))))

(declare-var y (_ BitVec 8))
(constraint (= (f y) (bvadd y y)))
(check-synth)
