-- Sample domain: REPLACE WITH YOUR DESIGN.
-- assemble.sh splits this file at the `-- @carriers` line.  The lines
-- above it are the domain types: the carrier kits of domain/carriers.txt
-- use them.  The lines below it are the domain operations: they can use
-- the carrier kits.  The sample has an enum with three constructors, one
-- record read by `match`, one list carrier and one operation that uses
-- fold and filter.

-- @section domain types
mu Color : Type 0 :=
  | red : Color
  | green : Color
  | blue : Color

-- A record is a `mu` family with one constructor, read by `match`.  Do not
-- use a Sigma for a record: the kernel has no Sigma pattern (P5).
mu Item : Type 0 :=
  | mkItem (c : Color) (n : Nat) : Item

def color : Item -> Color := fun (i : Item) =>
  match i as w in Item return Color with
  | mkItem c n => c

def amount : Item -> Nat := fun (i : Item) =>
  match i as w in Item return Nat with
  | mkItem c n => n

-- @carriers

-- @section domain operations
def colorIsRed : Color -> Option (prod ()) := fun (c : Color) =>
  match c as w in Color return Option (prod ()) with
  | red => inj 1 of 2 (tuple ())
  | green => inj 0 of 2 (tuple ())
  | blue => inj 0 of 2 (tuple ())

def isRed : Item -> Option (prod ()) := fun (i : Item) => colorIsRed (color i)

def total : Items -> Nat :=
  foldItems Nat (fun (i : Item) (n : Nat) => natAdd (amount i) n) 0

def redTotal : Items -> Nat := fun (xs : Items) => total (filterItems isRed xs)
