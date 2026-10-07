-- Sample program: REPLACE WITH YOUR OWN.  Each `EqX L v := reflX v` line
-- is a refl candidate: the kernel accepts it only when `v` is the normal
-- form of `L` (host CAPABILITY.md, P3).
def a : Item := mkItem red 2
def b : Item := mkItem green 3
def c : Item := mkItem red 4
def xs : Items := consItems a (consItems b (consItems c nilItems))

-- fold and filter
def allSum : EqNat (total xs) 9 := reflNat 9
def redSum : EqNat (redTotal xs) 6 := reflNat 6

-- map, pure, append and bind of the list carrier
def double : Item -> Item := fun (i : Item) =>
  mkItem (color i) (natAdd (amount i) (amount i))
def doubled : EqNat (total (mapItems double xs)) 18 := reflNat 18
def twice : EqNat (total (bindItems xs (fun (i : Item) => appendItems (pureItems i) (pureItems i)))) 18 := reflNat 18

-- the record and the Eq carrier
def firstColor : EqColor (color a) red := reflColor red
def sameColor : EqColor (color a) (color c) := reflColor red

-- the bridge from EqNat to EqColor
def pick : Nat -> Color := fun (n : Nat) =>
  case natLt n 2 with
  | 0 (u : prod ()) => blue
  | 1 (u : prod ()) => red
def picked : EqColor (pick 1) (pick 1) := congNatColor pick 1 1 (reflNat 1)

-- the header parameter and Le (a Sigma with literal values)
def underLimit : Le (total xs) limit := (0, reflNat 9)
