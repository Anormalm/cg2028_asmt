"""Compile and emulate actual ARM code. Requires unicorn and pyelftools.

python tests/verify_core.py --toolchain PATH_TO_ARM_GCC_BIN
Tests are synthetic, not a substitute for the supplied unit tests or board trials.
Build artifacts go into a temporary directory, outside the CubeIDE project.
"""
import argparse
import io
from pathlib import Path
import random
import shutil
import subprocess
import tempfile

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R4,
    UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
    UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
)

ROOT = Path(__file__).resolve().parents[1]
SAVED = [UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11]
STOP = 0xF0000
STACK = 0x2000F000


def load(path):
    elf = ELFFile(io.BytesIO(path.read_bytes()))
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.mem_map(0x10000, 0x100000)
    uc.mem_map(0x20000000, 0x10000)
    for segment in elf.iter_segments():
        if segment['p_type'] == 'PT_LOAD':
            uc.mem_write(segment['p_vaddr'], segment.data())
    symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    return uc, symbols


def call(uc, address, args):
    for reg, value in zip([UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2], args):
        uc.reg_write(reg, value & 0xFFFFFFFF)
    for i, reg in enumerate(SAVED):
        uc.reg_write(reg, 0x13570000 + i)
    uc.reg_write(UC_ARM_REG_SP, STACK)
    uc.reg_write(UC_ARM_REG_LR, STOP | 1)
    uc.emu_start(address | 1, STOP, count=4000000)
    assert uc.reg_read(UC_ARM_REG_PC) == STOP, 'Function did not return'
    assert uc.reg_read(UC_ARM_REG_SP) == STACK, 'Stack pointer changed'
    for i, reg in enumerate(SAVED):
        assert uc.reg_read(reg) == 0x13570000 + i, f'Callee-saved register {i+4} changed'
    value = uc.reg_read(UC_ARM_REG_R0)
    return value if value < 2**31 else value - 2**32


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--toolchain', type=Path)
    args = parser.parse_args()
    gcc = str(args.toolchain / 'arm-none-eabi-gcc') if args.toolchain else shutil.which('arm-none-eabi-gcc')
    if not gcc:
        parser.error('Supply --toolchain or put arm-none-eabi-gcc on PATH')
    with tempfile.TemporaryDirectory(prefix='cg2028-tests-') as directory:
        directory = Path(directory)
        linker = directory / 'test.ld'
        linker.write_text('''SECTIONS {
          . = 0x10000;
          .text : { *(.text*) *(.rodata*) }
          .ARM.exidx : { *(.ARM.exidx*) }
          . = 0x20000000;
          .data : { *(.data*) }
          .bss : { *(.bss*) *(COMMON) }
        }''')
        elf = directory / 'tests.elf'
        subprocess.run([gcc, '-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=soft',
                        '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-ffreestanding', '-nostdlib', '-fno-builtin',
                        '-I' + str(ROOT / 'Core/Inc'),
                        str(ROOT / 'tests/detector_cases.c'),
                        str(ROOT / 'tests/alert_ui_cases.c'),
                        str(ROOT / 'Core/Src/mov_avg.s'),
                        '-T' + str(linker), '-lc', '-lgcc', '-o', str(elf)], check=True)
        uc, symbols = load(elf)
        edges = [-2**31, -2**31+1, -1000000, -101, -100, -99, -1,
                 0, 1, 99, 100, 101, 1000000, 2**31-2, 2**31-1]
        rng = random.Random(2028)
        vectors = [(x, y, a) for x in edges for y in edges for a in range(101)]
        vectors += [(rng.randrange(-2**31, 2**31), rng.randrange(-2**31, 2**31),
                     rng.randrange(101)) for _ in range(10000)]
        for x, y, a in vectors:
            numerator = a*x + (100-a)*y
            expected = (abs(numerator)//100) * (-1 if numerator < 0 else 1)
            actual = call(uc, symbols['ewma_filter'], [x, y, a])
            assert actual == expected, (x, y, a, actual, expected)
        print(f'PASS: {len(vectors)} actual ARM EWMA cases, including register/stack preservation')
        for scenario in range(14):
            result = call(uc, symbols['test_entry'], [scenario])
            assert result == 0, f'Detector scenario {scenario}, C source line {result}'
        print('PASS: 14 detector scenarios (synthetic filtered inputs)')
        result = call(uc, symbols['alert_ui_test'], [])
        assert result == 0, f'Alert UI test failed at C line {result}'
        print('PASS: SOS, buzzer escalation, release gating and tick wraparound')


if __name__ == '__main__':
    main()
