#!/usr/bin/env python3
"""Run the bytecode of src/evm.c in geth evm against balances computed here.

Needs tcc, geth evm 1.14.12 and foundry cast. Mapping slots come from
`cast index` and selectors from `cast sig`, not from src/keccak.c."""
import functools
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / '.gatework/settlement-tcc'
SENDER = '7e5f4552091a69125d5dfcb7b8c2659029395bdf'
RECEIVER = '0000000000000000000000007265636569766572'
GAS = 16_777_216
CONFIG = {name + 'Block': 0 for name in (
    'homestead', 'eip150', 'eip155', 'eip158', 'byzantium', 'constantinople',
    'petersburg', 'istanbul', 'berlin', 'london', 'mergeNetsplit')}
CONFIG.update(chainId=1, terminalTotalDifficulty=0, cancunTime=0, shanghaiTime=0)
GENESIS = dict(config=CONFIG, coinbase='0x' + '00' * 20, difficulty='0x0', gasLimit='0x1000000',
               nonce='0x0000000000000000', timestamp='0x0', number='0x0',
               excessBlobGas='0x0', blobGasUsed='0x0')
CODES = (3, 3, 2, 2, 3, 3, 2, 1, 1, 1)
LEDGER, COUNT, PAYER, PAYEE, AMOUNT = range(5)


def require(ok, message):
    if not ok:
        raise ValueError(message)


def tool(*args):
    argv = ['tcc', '-Isrc', 'src/evm.c', 'src/keccak.c', 'domain/entries.c', '-run', 'test/evmtool.c', *map(str, args)]
    return subprocess.run(argv, cwd=ROOT, text=True, capture_output=True, timeout=60)


def bytecode(*args):
    result = tool(*args)
    require(result.returncode == 0 and result.stderr == '', f'evmtool {args}: {result.stderr}')
    text = result.stdout
    require(text.endswith('\n') and text.count('\n') == 1 and text.strip() == text.strip().lower(),
            f'evmtool {args}: not one line of lowercase hex')
    return text.strip()


def checked(argv):
    result = subprocess.run(argv, text=True, capture_output=True, timeout=60)
    require(result.returncode == 0, f'{argv[:3]}: {result.stderr}')
    return result.stdout


@functools.cache
def slot(base, key):
    return int(checked(['cast', 'index', 'uint256', str(key), str(base)]).strip(), 16)


@functools.cache
def selector(name, words):
    signature = name + '(' + ','.join(['uint256'] * words) + ')'
    return checked(['cast', 'sig', signature]).strip()[2:]


def data(name, *values):
    return selector(name, len(values)) + ''.join(f'{value:064x}' for value in values)


def objects(text):
    decoder, tail, result = json.JSONDecoder(), text.lstrip(), []
    while tail:
        value, end = decoder.raw_decode(tail)
        result.append(value)
        tail = tail[end:].lstrip()
    return result


def words(slots):
    return {int(key, 16): int(value, 16) for key, value in slots.items() if int(value, 16)}


def run(name, code, calldata, *, before=None, value=0, create=False):
    state = dict(GENESIS, alloc={
        SENDER: dict(balance=hex(10**24)),
        RECEIVER: dict(balance='0x0', storage={f'0x{k:064x}': f'0x{v:064x}'
                                               for k, v in (before or {}).items() if v})})
    genesis = WORK / (name + '-prestate.json')
    genesis.write_text(json.dumps(state))
    argv = ['evm', '--verbosity', '0', 'run', '--prestate', str(genesis), '--gas', str(GAS),
            '--sender', '0x' + SENDER, '--receiver', '0x' + RECEIVER, '--code', code,
            '--input', calldata, '--value', str(value), '--json', '--dump']
    text = checked(argv + (['--create'] if create else []))
    (WORK / (name + '.out')).write_text(text)
    records = objects(text)
    require(len(records) >= 2 and 'accounts' in records[-1], f'{name}: missing state dump')
    errors = [row['error'] for row in records if row.get('error')]
    require(all(error == 'execution reverted' for error in errors), f'{name}: EVM fault {errors}')
    stores = {key.lower().removeprefix('0x'): words(account.get('storage', {}))
              for key, account in records[-1]['accounts'].items()}
    return dict(status='revert' if errors else 'success',
                output=records[-2]['output'].lower().removeprefix('0x'),
                storage=stores.get(RECEIVER, {}),
                created={key: value for key, value in stores.items() if value and key != RECEIVER})


def expect(name, code, calldata, before, after, result, *, value=0):
    actual = run(name, code, calldata, before=before, value=value)
    wanted = dict(status='revert' if result is None else 'success',
                  output='' if result is None else f'{result:064x}',
                  storage={k: v for k, v in after.items() if v})
    got = {key: actual[key] for key in wanted}
    require(got == wanted, f'{name}: EVM {got} != {wanted}')


def tally_index(members, release, refund):
    return sum(members + 1 - r for r in range(release)) + refund


def verdict(members, codes, ballots):
    return codes[tally_index(members, ballots.count(1), ballots.count(2))]


def deploy(name, creation, runtime):
    made = run(name, creation, '', create=True)
    require(made['status'] == 'success' and made['output'] == runtime,
            f'{name}: creation did not return the runtime')
    require(made['storage'] == {} and made['created'] == {}, f'{name}: creation wrote storage')
    paid = run(name + '-value', creation, '', value=1, create=True)
    require(paid['status'] == 'revert' and paid['output'] == '', f'{name}: creation took a value')


def refusals():
    rows = [(('runtime', 0, 'debreu', 3), 'EVM_LIMIT'),
            (('runtime', 64, 'debreu', *([1] * 2145)), 'EVM_LIMIT'),
            (('runtime', 63, 'debreu', *([1] * 2080)), 'EVM_SIZE'),
            (('-k', 4, 'runtime', 16, 'debreu', *([1] * 969)), 'EVM_LIMIT'),
            (('runtime', 0, 'impossibility'), 'EVM_LIMIT'),
            (('runtime', 3, 'debreu', *CODES[:-1]), 'EVM_TABLE'),
            (('runtime', 3, 'debreu', *CODES[:-1], 4), 'EVM_TABLE'),
            (('creation', 3, 'debreu', 0, *CODES[1:]), 'EVM_TABLE'),
            (('runtime', 3, 'impossibility', 1), 'EVM_TABLE')]
    for args, code in rows:
        result = tool(*args)
        require(result.returncode == 1 and result.stdout == '' and
                result.stderr.startswith(f'langc: {code}: ') and result.stderr.count('\n') == 1,
                f'refusal {args[:3]}: {result.returncode} {result.stderr!r}')
    usage = tool('runtime', 'x', 'debreu')
    require(usage.returncode == 2, 'evmtool usage exit')
    return len(rows)


def debreu_cases(runtime):
    cases = 0
    initial = {}
    for decision, ballots in ((1, (1, 1, 3)), (2, (2, 2, 3)), (3, (3, 3, 3))):
        expect(f'cast-{decision}', runtime, data('cast', *ballots), initial, initial, decision)
        cases += 1
        for same in (False, True):
            payer, payee = 17, 17 if same else 34
            for balance in (4, 20):
                before = {COUNT: 1, slot(PAYER, 0): payer, slot(PAYEE, 0): payee,
                          slot(AMOUNT, 0): 5, slot(LEDGER, payee): 10, slot(LEDGER, payer): balance}
                after = dict(before)
                result = None
                if balance >= 5:
                    result = decision
                    after[slot(LEDGER, payer)] -= 5 if decision in (1, 2) else 0
                    after[slot(LEDGER, payee)] += 5 if decision == 1 else 0
                expect(f'settle-{decision}-{same}-{balance}', runtime,
                       data('settle', 0, *ballots), before, after, result)
                cases += 1
    before = {COUNT: 1, slot(PAYER, 0): 17, slot(PAYEE, 0): 34, slot(AMOUNT, 0): 5,
              slot(LEDGER, 17): 20, slot(LEDGER, 34): 2**256 - 1}
    expect('release-overflow', runtime, data('settle', 0, 1, 1, 3), before, before, None)
    for ballot in (0, 4):
        expect(f'bad-ballot-{ballot}', runtime, data('settle', 0, ballot, 1, 3), before, before, None)
    return cases + 3


def entry_cases(runtime, regime):
    claim = {slot(LEDGER, 17): 3}
    after = {slot(LEDGER, 17): 8, COUNT: 1, slot(PAYER, 0): 17, slot(PAYEE, 0): 34, slot(AMOUNT, 0): 5}
    expect(f'{regime}-deposit', runtime, data('deposit', 17, 34, 5), claim, after, 0, value=7)
    second = {**after, slot(LEDGER, 34): 1, COUNT: 2, slot(PAYER, 1): 34, slot(PAYEE, 1): 17,
              slot(AMOUNT, 1): 1}
    expect(f'{regime}-deposit-next', runtime, data('deposit', 34, 17, 1), after, second, 1, value=1)
    rows = [('short-value', data('deposit', 17, 34, 5), 4, claim),
            ('payer-range', data('deposit', 2**160, 34, 0), 0, claim),
            ('payee-range', data('deposit', 17, 2**160, 0), 0, claim),
            ('overflow', data('deposit', 17, 34, 5), 5, {slot(LEDGER, 17): 2**256 - 3}),
            ('short-data', data('deposit', 17, 34, 5)[:-2], 5, claim),
            ('no-selector', 'aabbcc', 0, claim),
            ('unknown', 'ffffffff', 0, claim)]
    for label, calldata, value, before in rows:
        expect(f'{regime}-{label}', runtime, calldata, before, before, None, value=value)
    return 2 + len(rows)


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    require(bytecode('keccak', '') ==
            'c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470', 'keccak256("")')
    require(bytecode('keccak', 'transfer(address,uint256)')[:8] == 'a9059cbb', 'transfer selector')
    cases = refusals()
    runtime = bytecode('runtime', 3, 'debreu', *CODES)
    deploy('debreu-deploy', bytecode('creation', 3, 'debreu', *CODES), runtime)
    cases += debreu_cases(runtime) + entry_cases(runtime, 'debreu')
    packed = sum(code * 4**index for index, code in enumerate(CODES))
    expect('amend', runtime, data('amend'), {}, {}, packed)
    expect('amend-value', runtime, data('amend'), {}, {}, None, value=1)
    expect('cast-value', runtime, data('cast', 1, 1, 3), {}, {}, None, value=1)
    expect('settle-short', runtime, data('settle', 0, 1, 1, 3)[:-64], {}, {}, None)
    cases += 4
    impossible = bytecode('runtime', 3, 'impossibility')
    deploy('impossibility-deploy', bytecode('creation', 3, 'impossibility'), impossible)
    cases += entry_cases(impossible, 'impossibility')
    expect('impossibility-cast', impossible, data('cast', 1, 1, 3), {}, {}, None)
    cases += 1
    for members, codes, vectors in (
            (1, (3, 2, 1), [(1,), (2,), (3,)]),
            (14, tuple(i % 3 + 1 for i in range(120)),
             [tuple((m * k) % 3 + 1 for m in range(14)) for k in range(5)] + [(1,) * 14, (2,) * 14]),
            (15, tuple(i % 3 + 1 for i in range(136)),
             [tuple((m * k) % 3 + 1 for m in range(15)) for k in range(3)]),
            (62, tuple(i % 3 + 1 for i in range(2016)),
             [tuple((m * k) % 3 + 1 for m in range(62)) for k in range(3)] + [(3,) * 62])):
        code = bytecode('runtime', members, 'debreu', *codes)
        for k, ballots in enumerate(vectors):
            expect(f'cast-n{members}-{k}', code, data('cast', *ballots), {}, {},
                   verdict(members, codes, ballots))
            cases += 1
        packed = sum(c * 4**index for index, c in enumerate(codes)) if 2 * len(codes) <= 256 else None
        expect(f'amend-n{members}', code, data('amend'), {}, {}, packed)
        word = '' if packed is None else f'{packed:x}'.zfill(len(f'{packed:x}') + len(f'{packed:x}') % 2)
        body = '5f5ffd' if packed is None else f'{0x5f + len(word) // 2:02x}{word}5f5260205ff3'
        require(bytecode('amend', members, 'debreu', *codes) == body, f'evmtool amend n={members}')
        cases += 1
    print(f'SETTLEMENT cases={cases} deploy=2 geth=expected OK (logs: {WORK})')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'SETTLEMENT FAIL: {error}', file=sys.stderr)
        sys.exit(1)
