#!/usr/bin/env python3
"""Runs EVM runtime code with `evm run`, one process for each call.

Usage: run-evm.py CODE [--value N] [--prestate FILE] [--input HEX | --abi FILE CALL...]
CODE is a hex file or a 0x literal. CALL is `NAME ARGS...`, or `-` for one
`NAME ARGS...` line for each call on stdin. FILE of --prestate is a genesis
file for `evm run` (test/prestate.json gives the default sender a balance).
Prints the result word in decimal, or `trap` on a revert or another error.
evm writes the result on stdout and its log and errors on stderr.
"""
import argparse
import subprocess
import sys


def run(code, data, value, prestate):
    state = ["--prestate", prestate] if prestate else []
    out = subprocess.run(["evm", "--code", code, "--input", data, "--value", str(value)] + state + ["run"],
                         capture_output=True, text=True, check=False)
    lines = [line for line in out.stdout.splitlines() if line.strip()]
    failed = out.returncode != 0 or "error" in out.stdout + out.stderr or not lines
    return "trap" if failed else str(int(lines[-1].strip()[2:] or "0", 16))


def calldata(selectors, words):
    name, args = words[0], words[1:]
    if name not in selectors or any(int(arg) >= 2 ** 256 for arg in args):
        return None
    return selectors[name] + "".join("%064x" % int(arg) for arg in args)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("code")
    parser.add_argument("--value", default="0")
    parser.add_argument("--prestate")
    parser.add_argument("--input")
    parser.add_argument("--abi")
    parser.add_argument("call", nargs="*")
    opt = parser.parse_args()
    code = opt.code[2:] if opt.code.startswith("0x") else open(opt.code).read().strip()
    if opt.input is not None:
        print(run(code, opt.input, opt.value, opt.prestate))
        return 0
    selectors = dict(line.split() for line in open(opt.abi) if line.strip())
    calls = [line.split() for line in sys.stdin if line.strip()] if opt.call == ["-"] else [opt.call]
    for words in calls:
        data = calldata(selectors, words)
        print("no-entry" if data is None else run(code, data, opt.value, opt.prestate), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
