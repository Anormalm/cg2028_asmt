"""Exercise the built ARM PWM driver against emulated register memory.

Run after scripts/build_firmware.py. This verifies register programming, not
electrical output: Unicorn does not simulate timer waveforms or GPIO circuitry.
"""
import io
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from verify_core import call

root = Path(__file__).resolve().parents[1]
elf = ELFFile(io.BytesIO((root/'build/CG2028_Enhancements.elf').read_bytes()))
uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
for address, size in [(0x08000000, 0x200000), (0x20000000, 0xA0000),
                      (0xF0000, 0x1000), (0x40000000, 0x30000), (0x48000000, 0x3000)]:
    uc.mem_map(address, size)
for segment in elf.iter_segments():
    if segment['p_type'] == 'PT_LOAD':
        uc.mem_write(segment['p_vaddr'], segment.data())
symbols = {s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
def read(address): return struct.unpack('<I', uc.mem_read(address, 4))[0]
def write(address, value): uc.mem_write(address, struct.pack('<I', value))
def invoke(name, *args): call(uc, symbols[name], args)
tim = 0x40000400  # TIM3
gpio = 0x48000400  # GPIOB
write(symbols['SystemCoreClock'], 80000000)
invoke('Buzzer_Stop')  # Also safe before peripheral initialization.
invoke('Buzzer_Init')
assert read(tim+0x28) == 79  # PSC: 80 MHz -> 1 MHz
assert (read(gpio) >> 2) & 3 == 2  # PB1 alternate-function mode
assert (read(gpio+0x20) >> 4) & 15 == 2  # AF2
assert read(tim+0x20) & (1 << 12)  # CC4E
for hz in [1047, 1319, 1568, 2093, 2637, 100, 5000]:
    invoke('Buzzer_SetFrequency', hz)
    period = read(tim+0x2C)+1
    assert abs(1000000/period-hz)/hz < 0.005
    assert read(tim+0x40) == period//2  # CCR4
    assert read(tim) & 1
    write(tim+0x24, 123)  # Repeating a note must not restart its counter.
    invoke('Buzzer_SetFrequency', hz)
    assert read(tim+0x24) == 123
for hz in [0, 99, 5001, 0xFFFFFFFF]:
    invoke('Buzzer_SetFrequency', 1568)
    invoke('Buzzer_SetFrequency', hz)
    assert read(tim+0x40) == 0 and not (read(tim) & 1)
    assert read(symbols['buzzer_frequency_hz']) == 0
write(0x40021008, 4 << 8)  # APB1 /2: timer clock doubles PCLK1.
invoke('Buzzer_Init')
assert read(tim+0x28) == 79
print('PASS: actual ARM PWM driver pin mapping, frequencies, duty, rest, bounds and APB prescaling')
