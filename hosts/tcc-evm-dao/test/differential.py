#!/usr/bin/env python3
"""Compare the langc checker with the contract that langc writes.

The checker side is `langc verdicts PROG F`: one digit per ballot vector,
in the order of itertools.product over the codes 1 to k (k = 3 unless
`--decisions K`), with the first ballot outermost. The contract side deploys the creation code of `langc
build` in geth evm, then runs `amend`, and `cast` and `settle` on each
ballot vector. Each `cast` and `settle` result must equal the checker digit,
and `amend` must return the packed `langc table` codes. No verdict comes
from Python. Run `make` first.

usage: python3 test/differential.py [--program PROG] [--langc LANGC --decisions K]
"""
import argparse
import itertools
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys

import settlement as S

ROOT = Path(__file__).resolve().parent.parent
LANGC = ROOT / 'build/langc'
WORK = ROOT / '.gatework/differential'
PROGRAM = ROOT / 'examples/arrow-debreu.lang'
CODES = (1, 2, 3)
PAYER, PAYEE, AMOUNT = 17, 34, 5


def langc(*args, lines=1):
    result = subprocess.run([str(LANGC), *map(str, args)], text=True,
                            capture_output=True, timeout=120)
    S.require(result.returncode == 0 and result.stderr == '',
              f'langc {args[0]}: exit {result.returncode}: {result.stderr.strip()[:400]}')
    S.require(result.stdout.count('\n') == lines and result.stdout.endswith('\n' * lines),
              f'langc {args[0]}: not {lines} line(s) on stdout')
    return result.stdout.strip()


def members(program):
    first = program.read_text().splitlines()[0]
    found = re.fullmatch(r'def members : Nat := ([0-9]+)', first)
    S.require(found is not None, f'line 1 of {program} must be def members : Nat := N')
    return int(found.group(1))


def table(program, size):
    words = langc('table', program).split()
    S.require(words[:2] == ['debreu', str(size)], f'table is not debreu {size}: {words[:2]}')
    codes = tuple(map(int, words[2:]))
    tallies = math.comb(size + len(CODES) - 1, len(CODES) - 1)
    S.require(len(codes) == tallies and set(codes) <= set(CODES),
              f'table has {len(codes)} codes, not one code in 1..{len(CODES)} per tally')
    return codes


def checker_verdicts(program, vectors):
    digits = langc('verdicts', program, 'F')
    S.require(len(digits) == len(vectors) and set(digits) <= set(''.join(map(str, CODES))),
              f'verdicts gave {len(digits)} digits for {len(vectors)} vectors')
    return dict(zip(vectors, map(int, digits)))


def contract(program):
    def part(name, *flags):
        out = WORK / f'{name}.hex'
        langc('build', program, *flags, '-o', out, lines=0)
        return out.read_text().strip()

    creation, runtime = part('creation'), part('runtime', '--runtime')
    S.deploy('differential-deploy', creation, runtime)
    return runtime


def claim():
    return {S.COUNT: 1, S.slot(S.PAYER, 0): PAYER, S.slot(S.PAYEE, 0): PAYEE,
            S.slot(S.AMOUNT, 0): AMOUNT, S.slot(S.LEDGER, PAYER): 20,
            S.slot(S.LEDGER, PAYEE): 10}


def settled(code):
    # Design section 3: release moves the amount, refund debits it, hold keeps it.
    before = claim()
    debit = AMOUNT if code in (1, 2) else 0
    credit = AMOUNT if code == 1 else 0
    return {**before, S.slot(S.LEDGER, PAYER): before[S.slot(S.LEDGER, PAYER)] - debit,
            S.slot(S.LEDGER, PAYEE): before[S.slot(S.LEDGER, PAYEE)] + credit}


def main():
    global LANGC, CODES, WORK
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--program', type=Path, default=PROGRAM)
    parser.add_argument('--langc', type=Path, default=LANGC)
    parser.add_argument('--decisions', type=int, choices=range(2, 10), default=len(CODES))
    args = parser.parse_args()
    program = args.program.resolve()
    LANGC, CODES = args.langc.resolve(), tuple(range(1, args.decisions + 1))
    WORK = WORK / program.stem
    S.require(shutil.which('evm') and shutil.which('cast'), 'evm and cast are required')
    S.require(LANGC.exists(), f'{LANGC} is missing: run make')
    WORK.mkdir(parents=True, exist_ok=True)
    S.WORK = WORK
    size = members(program)
    vectors = tuple(itertools.product(CODES, repeat=size))
    codes = table(program, size)
    verdicts = checker_verdicts(program, vectors)
    runtime = contract(program)
    # amend packs k.bit_length() bits per code, code 0 at the low end.
    bits = len(CODES).bit_length()
    packed = sum(code << (bits * index) for index, code in enumerate(codes))
    S.expect('differential-amend', runtime, S.data('amend'), {}, {}, packed)
    for index, (vector, code) in enumerate(verdicts.items()):
        S.expect(f'differential-cast-{index}', runtime, S.data('cast', *vector), {}, {}, code)
        S.expect(f'differential-settle-{index}', runtime, S.data('settle', 0, *vector),
                 claim(), settled(code), code)
    print(f'DIFFERENTIAL vectors={len(vectors)} '
          f'codes={"".join(map(str, verdicts.values()))} amend,cast,settle geth=langc OK '
          f'(logs: {WORK})')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'DIFFERENTIAL FAIL: {error}', file=sys.stderr)
        sys.exit(1)
