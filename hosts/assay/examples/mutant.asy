-- Mutant of examples/program.asy: REPLACE WITH YOUR OWN.  gate.sh expects
-- the kernel to refuse this program with the text of the `expect:` line.
-- The refusal prints the normal form (host CAPABILITY.md, P3).
-- expect: gives the index 8 and the type asks for 9
def a : Item := mkItem red 2
def b : Item := mkItem green 3
def c : Item := mkItem red 4
def xs : Items := consItems a (consItems b (consItems c nilItems))
def allSum : EqNat (total xs) 9 := reflNat 8
