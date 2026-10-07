# Domain interface

Replace the sample inventory domain with the design of your language.
`schema.mech` declares the domain families and the JSON header name.
`plans.mech` tells the generic compiler how to check and emit their values.
Both files must describe the same constructors and field order.

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

Names in the sample tables are UTF-8 byte chains of `textByte` and
`textEnd`. The tables are hand written. No generator is required.
The test suite checks constructor results against the sample schema and
checks family separation, argument types and reserved names. Adapt those
tests when you change the domain.

Generic value-indexed families are planned. The core does not include
the original language's Ref, Query or WritePath forms.
