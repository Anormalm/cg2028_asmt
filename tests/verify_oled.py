"""Test actual compiled OLED code with mocked I2C; no electrical/display claims."""
import runpy
from pathlib import Path
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_PC, UC_ARM_REG_LR

g = runpy.run_path(str(Path(__file__).with_name('verify_buzzer_driver.py')))
uc, symbols, read, invoke = (g[k] for k in ['uc', 'symbols', 'read', 'invoke'])
transfers = []
fail = False

def transmit(emu, address, size, user):
    assert emu.reg_read(UC_ARM_REG_R1) == 0x78
    length = emu.reg_read(UC_ARM_REG_R3)
    timeout = read(emu.reg_read(UC_ARM_REG_SP))
    assert timeout == 25
    payload = bytes(emu.mem_read(emu.reg_read(UC_ARM_REG_R2), length))
    transfers.append(payload)
    emu.reg_write(UC_ARM_REG_R0, 1 if fail else 0)
    emu.reg_write(UC_ARM_REG_PC, emu.reg_read(UC_ARM_REG_LR))

address = symbols['HAL_I2C_Master_Transmit'] & ~1
uc.hook_add(UC_HOOK_CODE, transmit, begin=address, end=address)
def framebuffer():
    return bytes(uc.mem_read(symbols['OLED_Buffer'], 1024))
def status(value, now):
    invoke('OLED_SetStatus', value)
    invoke('OLED_Service', now)
    return framebuffer()

invoke('OLED_Init')
assert read(symbols['oled_online']) == 1
commands = [p[1] for p in transfers if p[0] == 0]
assert any(commands[i:i+2] == [0x20, 0x02] for i in range(len(commands)-1)), 'page addressing'
assert any(commands[i:i+2] == [0xDA, 0x12] for i in range(len(commands)-1)), '64-row COM setup'
assert sum(len(p)-1 for p in transfers if p[0] == 0x40) == 1024
normal = status(0, 0)
assert any(normal)
count = len(transfers)
status(0, 1500)
assert len(transfers) == count, 'unchanged normal screen must not redraw'
fall = status(1, 2000)
frown = status(1, 3000)
assert fall != frown and normal != fall and normal != frown
assert status(1, 4000) == fall
sos = status(2, 4000)
assert sos not in (normal, fall, frown), 'manual SOS must not claim detected fall'
assert status(2, 5000) == frown
assert status(0, 5100) == normal, 'local acknowledgement clears alarm display'
# Failed transaction aborts the rest of a frame, goes offline and counts once.
fail = True
count = len(transfers)
errors = read(symbols['oled_errors'])
status(1, 6000)
assert len(transfers) == count+1
assert read(symbols['oled_online']) == 0
assert read(symbols['oled_errors']) == errors+1
status(0, 6100)
assert len(transfers) == count+1
invoke('OLED_Init')
assert len(transfers) == count+2 and read(symbols['oled_online']) == 0
# Task retries initialization; recovered display redraws current state.
fail = False
invoke('OLED_Init')
assert read(symbols['oled_online']) == 1
assert status(0, 12000) == normal
print('PASS: compiled OLED setup, complete frames, normal/fall/SOS/ACK, redraw suppression, finite timeout, failure and recovery')
