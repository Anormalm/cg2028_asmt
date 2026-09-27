"""Exercise built sensor processing with synthetic DMA frames, no real hardware.
Requires a command-line firmware build, unicorn and pyelftools.
"""
import runpy, struct
from pathlib import Path
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_PC, UC_ARM_REG_LR
# Reuse the ELF loader and verify timer setup before testing sensor integration.
g = runpy.run_path(str(Path(__file__).with_name('verify_buzzer_driver.py')))
uc, symbols, read, write, invoke = (g[k] for k in ['uc','symbols','read','write','invoke'])
uc.mem_map(0xE000E000,0x2000)
uc.reg_write(UC_ARM_REG_C1_C0_2,0x00F00000)
uc.reg_write(UC_ARM_REG_FPEXC,0x40000000)
# Single-threaded emulation has no RTOS interrupts; bypass BASEPRI operations.
def critical(emu,address,size,user): emu.reg_write(UC_ARM_REG_PC,emu.reg_read(UC_ARM_REG_LR))
for name in ['vPortEnterCritical','vPortExitCritical']:
    address=symbols[name]&~1
    uc.hook_add(UC_HOOK_CODE,critical,begin=address,end=address)
invoke('Buzzer_Stop')
base=symbols['current']
def frame(seq,amplitude,offset=0):
    values=[((amplitude if i%2 else -amplitude)+offset)*256 for i in range(256)]
    uc.mem_write(symbols['audio_latest'],struct.pack('<256i',*values))
    write(symbols['audio_sequence'],seq)
def field(offset,signed=False):
    v=read(base+offset)
    return v-2**32 if signed and v>=2**31 else v
frame(1,65536,10000)
invoke('ExtraSensors_Update',1000)
assert -303 <= field(16,True) <= -299, field(16,True)
assert field(20)==1 and field(8)==0
frame(2,131072)
invoke('ExtraSensors_Update',1020)
assert field(24)==1 and field(8)==1
# Brief peak survives a quiet frame, then is consumed rather than replayed.
frame(3,0)
invoke('ExtraSensors_Update',1030)
snapshot_at=0x20082000
invoke('ExtraSensors_Get',snapshot_at)
peak=struct.unpack('<i',uc.mem_read(snapshot_at+16,4))[0]
assert -243 <= peak <= -239,peak
invoke('ExtraSensors_Get',snapshot_at)
assert struct.unpack('<i',uc.mem_read(snapshot_at+16,4))[0]==-960
# Buzzer contamination suppresses activity counts even if the frame is loud.
write(symbols['buzzer_frequency_hz'],880)
frame(4,131072)
invoke('ExtraSensors_Update',1040)
assert field(28)==1 and field(24)==0 and field(8)==1
write(symbols['buzzer_frequency_hz'],0)
frame(5,131072)
invoke('ExtraSensors_Update',1200)
assert field(28)==1 and field(8)==1
frame(6,0,20000)
invoke('ExtraSensors_Update',1600)
assert field(16,True)==-960 and field(28)==0
frame(7,131072)
invoke('ExtraSensors_Update',3100)
assert field(8)==2
# Missing frames become unavailable, not a fabricated quiet measurement.
invoke('ExtraSensors_Update',3320)
assert field(20)==0
# Test real HTTP command parsing and incomplete TCP fragments.
response=b'HTTP/1.1 200 OK\r\n\r\nACK boot-s1\nSOUND 2 0 -450\n'
expected=b'ACK boot-s1\n'
response_at, expected_at=0x20080000,0x20081000
uc.mem_write(expected_at,expected+b'\0')
for n in range(len(response)):
    uc.mem_write(response_at,response[:n]+b'\0')
    assert g['call'](uc,symbols['ExtraSensors_ParseReply'],[response_at,expected_at])==0,n
uc.mem_write(response_at,response+b'\0')
assert g['call'](uc,symbols['ExtraSensors_ParseReply'],[response_at,expected_at])==1
frame(8,131072)
invoke('ExtraSensors_Update',3400)
assert field(4)==2 and field(20)==0  # Website disabled sound monitoring.
for invalid in [response.replace(b'SOUND 2 0',b'SOUND 2 2'),response.replace(b'-450',b'-999'),response.replace(b'boot-s1',b'boot-s2')]:
    uc.mem_write(response_at,invalid+b'\0')
    assert g['call'](uc,symbols['ExtraSensors_ParseReply'],[response_at,expected_at])==0
print('PASS: ARM audio RMS/DC rejection, buzzer masking, activity counts, dropout and fragmented website commands')

