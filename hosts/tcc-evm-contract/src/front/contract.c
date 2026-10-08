/* The contract prelude (slice K3a). front_load reads this text after
   domain/domain.lang, as domain text. Thus rule R1 allows the two families
   and rule R4 makes their names core names. */
#include "front/contract.h"

const char contract_source[] =
  "family Env := makeEnv (now : Nat) (caller : Addr)\n"
  "family Out := pay (payToken : Addr) (payTo : Addr) (payAmount : U256)\n"
  "  | pull (pullToken : Addr) (pullFrom : Addr) (pullTo : Addr) (pullAmount : U256)\n"
  "  | emit (emitTag : Nat) (emitFields : List U256)\n";

const size_t contract_source_len = sizeof contract_source - 1u;
