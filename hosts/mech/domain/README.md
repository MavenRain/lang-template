# Domain interface

Replace the sample inventory domain with the design of your language.
`schema.mech` declares the domain families and the JSON header name.
`plans.mech` tells the generic compiler how to check and emit their values.
`bin/gen-domain.mjs` generates `plans.mech` from the `mu` declarations in
`schema.mech`. Run `make plans` after each schema change. Do not edit
`plans.mech`. `make check` and the domain tests fail when it does not match
the schema.

| Entry | Type | Contract |
|---|---|---|
| domainName | Text | UTF-8 key for the JSON format version, currently sample-lang |
| domainType | Text -> Option LType | Return the canonical type for each public family name |
| domainConstructorPlan | Text -> Text -> Option Plan | Look up a constructor within the requested family |
| domainReservedName | Text -> Nat | Return 1 for every family and constructor name, otherwise 0 |

Unknown names return `none` from the two lookups. Domain names must not
collide with core forms or constructors. Use `tyNamed` for domain families.
The generic `LType` and `Plan` carriers are in `compiler/runtime.mech`.

`recordPlan argumentTypes fieldNames tag` checks the arguments in order
and emits a JSON object. Use `none` as its tag for a record family with
one constructor. For a tagged record, `some label` adds a `tag` field.
Do not also use `tag` as a field name.
`plan nil 14 label` emits a nullary enum constructor as a JSON string.
Keep enum labels distinct within a family. Other plan modes implement
generic primitives and are not needed for the sample domain.

The generator reads each family head of the form `mu Name : Type 0 with`
or `and Name : Type 0 with`. Each constructor field must have the form
`(name : Type)`. A field type is `Nat`, `Text`, `Flag`, `Value`, `Values`,
`Attrs`, a domain family, or `Option T` or `List T` of a field type.
A family with only nullary constructors becomes an enum. A family with one
constructor becomes a record with no tag. Each constructor of another
family becomes a tagged record, and a nullary one emits only its tag.
The generator refuses parameterized families, unknown types, repeated
names and a `tag` field in a tagged family. Names in the tables are UTF-8
byte chains of `textByte` and `textEnd`.

The test suite checks constructor results against the sample schema and
checks family separation, argument types and reserved names. Adapt those
tests when you change the domain.

Generic value-indexed families are planned. The core does not include
the original language's Ref, Query or WritePath forms.
