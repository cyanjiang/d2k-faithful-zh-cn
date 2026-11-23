"""Disassembles a range of the 1.06 exe: python re/disasm.py <exe> <start_hex> <count>"""
import struct
import sys

import capstone

sys.argv += []
exec(open(__file__.replace('disasm.py', 'dump_ui_tables.py')).read().split("def main")[0])


def run():
    d, off = load(sys.argv[1])
    start = int(sys.argv[2], 16)
    count = int(sys.argv[3])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    code = d[off(start):off(start) + count * 15]
    for i, ins in enumerate(md.disasm(code, start)):
        if i >= count:
            break
        print(f'{ins.address:08x}  {ins.mnemonic} {ins.op_str}')


run()
