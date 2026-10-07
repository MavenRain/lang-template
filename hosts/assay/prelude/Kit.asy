-- lang-template assay host kit: the domain-free prelude.
-- Source: escrow-lang 279214c, prelude/Prelude.asy sections 2 and 3, and
-- `Le` from section 7.  The escrow domain is removed.  assemble.sh writes
-- the header, this file, the domain types, the generated carrier kits
-- (gen/carrier.sh), the domain operations and the program, in that order.
-- Each `-- @section` line starts one section.  A check of each prefix
-- finds the first bad section.  Equality is one `mu` family for each
-- index type and each list is one `mu` family for each element type: a
-- constructor of a family with a parameter cannot appear in a term
-- (host CAPABILITY.md, P2).  Each constructor takes its index explicitly
-- (`reflNat 2`).

-- @section 1 Unit
def Unit : Type 0 := prod ()

def unit : Unit := tuple ()

-- @section 2 Equality on Nat
mu EqNat : (0 a : Nat) -> (0 b : Nat) -> Type 0 :=
  | reflNat : (0 x : Nat) -> EqNat x x

def transportNat : (0 P : Nat -> Type 0) -> (0 x : Nat) -> (0 y : Nat) -> EqNat x y -> P x -> P y :=
  fun (0 P : Nat -> Type 0) (0 x : Nat) (0 y : Nat) (e : EqNat x y) =>
    match e as q in EqNat i j return P i -> P j with
    | reflNat 0 z => fun (p : P z) => p

def symmNat : (0 x : Nat) -> (0 y : Nat) -> EqNat x y -> EqNat y x :=
  fun (0 x : Nat) (0 y : Nat) (e : EqNat x y) =>
    match e as q in EqNat i j return EqNat j i with
    | reflNat 0 z => reflNat z

def transNat : (0 x : Nat) -> (0 y : Nat) -> (0 z : Nat) -> EqNat x y -> EqNat y z -> EqNat x z :=
  fun (0 x : Nat) (0 y : Nat) (0 z : Nat) (e1 : EqNat x y) (e2 : EqNat y z) =>
    transportNat (fun (w : Nat) => EqNat x w) y z e2 e1

def congNat : (f : Nat -> Nat) -> (0 x : Nat) -> (0 y : Nat) -> EqNat x y -> EqNat (f x) (f y) :=
  fun (f : Nat -> Nat) (0 x : Nat) (0 y : Nat) (e : EqNat x y) =>
    match e as q in EqNat i j return EqNat (f i) (f j) with
    | reflNat 0 z => reflNat (f z)

-- @section 3 Order on Nat
-- `Le n m` holds when some `k` gives `natAdd n k = m`.  `natAdd` reduces
-- only on literals, so a proof of `Le` takes literal values (P6).
def Le : Nat -> Nat -> Type 0 := fun (n : Nat) (m : Nat) =>
  (k : Nat) * EqNat (natAdd n k) m

-- @section 4 Option and Sum
-- Generic type functions over the built-in `sum`.  Leg 0 of `Option A` is
-- none and leg 1 is some.  A test is `A -> Option (prod ())`: leg 1 keeps,
-- as leg 1 of `natEq` and `natLt` is true.
def Option : Type 0 -> Type 0 := fun (A : Type 0) => sum (prod (), A)

def none : (0 A : Type 0) -> Option A :=
  fun (0 A : Type 0) => inj 0 of 2 (tuple ())

def pureOption : (0 A : Type 0) -> A -> Option A :=
  fun (0 A : Type 0) (a : A) => inj 1 of 2 a

def mapOption : (0 A : Type 0) -> (0 B : Type 0) -> (A -> B) -> Option A -> Option B :=
  fun (0 A : Type 0) (0 B : Type 0) (f : A -> B) (o : Option A) =>
    case o with
    | 0 (u : prod ()) => inj 0 of 2 u
    | 1 (a : A) => inj 1 of 2 (f a)

def bindOption : (0 A : Type 0) -> (0 B : Type 0) -> Option A -> (A -> Option B) -> Option B :=
  fun (0 A : Type 0) (0 B : Type 0) (o : Option A) (f : A -> Option B) =>
    case o with
    | 0 (u : prod ()) => inj 0 of 2 u
    | 1 (a : A) => f a

def filterOption : (0 A : Type 0) -> (A -> Option (prod ())) -> Option A -> Option A :=
  fun (0 A : Type 0) (p : A -> Option (prod ())) (o : Option A) =>
    case o with
    | 0 (u : prod ()) => inj 0 of 2 u
    | 1 (a : A) =>
      (case p a with
       | 0 (u : prod ()) => inj 0 of 2 u
       | 1 (u : prod ()) => inj 1 of 2 a)

def Sum : Type 0 -> Type 0 -> Type 0 := fun (A : Type 0) (B : Type 0) => sum (A, B)

def pureSum : (0 E : Type 0) -> (0 A : Type 0) -> A -> Sum E A :=
  fun (0 E : Type 0) (0 A : Type 0) (a : A) => inj 1 of 2 a

def mapSum : (0 E : Type 0) -> (0 A : Type 0) -> (0 B : Type 0) -> (A -> B) -> Sum E A -> Sum E B :=
  fun (0 E : Type 0) (0 A : Type 0) (0 B : Type 0) (f : A -> B) (s : Sum E A) =>
    case s with
    | 0 (e : E) => inj 0 of 2 e
    | 1 (a : A) => inj 1 of 2 (f a)

def bindSum : (0 E : Type 0) -> (0 A : Type 0) -> (0 B : Type 0) -> Sum E A -> (A -> Sum E B) -> Sum E B :=
  fun (0 E : Type 0) (0 A : Type 0) (0 B : Type 0) (s : Sum E A) (f : A -> Sum E B) =>
    case s with
    | 0 (e : E) => inj 0 of 2 e
    | 1 (a : A) => f a
