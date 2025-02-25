
Hello, my name is Mihai, the title of my thesis is 
"Fast discard strategy for generating SyGuS programs"
and it aims at improving capabilities of current syntax-guided synthesis problems solvers present in program synthesis.

Syntax guided synthesis aims at generating programs based
on providing both syntactic and regular smt constraints.

We learn from the other top 3 solvers of this problem, cvc5, dryadSynth and LoopInvGen and their approaches:
    - smt techniques
    - concolic
    - symbolic 

First we explore the space of candidate programs using simmulated annealing and similar metaheuristic
to guide a random search,then we separate candidate programs and their intermediaries into failures or successes.

In the second part we use the cvc5 solver and perform the same process.

We use this information to guide the search in the future for instantly discarding the space of possible programs.

It can be useful for a wide-range of problems, most important being invariant generation.

#https://saswat.padhi.me/assets/pdf/pldi2016_pie.pdf
#https://engineering.purdue.edu/~xqiu/DryadSynth.pdf
#https://www.cis.upenn.edu/~alur/SyGuS13.pdf
#https://cseweb.ucsd.edu/~ldantoni/papers/cav18-qsygus.pdf
#https://arxiv.org/pdf/1711.10641#:~:text=We%20give%20an%20overview%20of%20recent%20techniques%20for,the%20core%20of%20Satisfiability%20Modulo%20Theories%20%28SMT%29%20solvers.
