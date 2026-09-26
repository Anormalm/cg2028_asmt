"""Reproducible Debug build without launching CubeIDE; no generated makefiles needed."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--toolchain', type=Path, required=True)
    args = parser.parse_args()
    gcc = str(args.toolchain / 'arm-none-eabi-gcc')
    build = ROOT / 'build'
    build.mkdir(exist_ok=True)
    tree = ET.parse(ROOT / '.cproject')
    entries = tree.findall('.//sourceEntries')[0]
    sources = []
    for entry in entries:
        base = ROOT / entry.attrib['name']
        excluded = entry.attrib.get('excluding', '').split('|')
        for path in base.rglob('*'):
            if path.suffix.lower() in ('.c', '.s') and path.relative_to(base).as_posix() not in excluded:
                sources.append(path)
    includes = ['Core/Inc', 'Core/RTOS/include', 'Core/RTOS/portable', 'Core/WiFi',
                'Drivers/BSP/Components/es_wifi', 'Drivers/STM32L4xx_HAL_Driver/Inc',
                'Drivers/STM32L4xx_HAL_Driver/Inc/Legacy',
                'Drivers/CMSIS/Device/ST/STM32L4xx/Include', 'Drivers/CMSIS/Include']
    machine = ['-mcpu=cortex-m4', '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard', '-mthumb']
    common = machine + ['-g3', '-O0', '-DDEBUG', '-DUSE_HAL_DRIVER', '-DSTM32L4S5xx',
                        '-ffunction-sections', '-fdata-sections', '-Wall', '--specs=nano.specs']
    common += ['-I' + str(ROOT / include) for include in includes]

    def compile_one(source):
        obj = build / (source.relative_to(ROOT).as_posix().replace('/', '_') + '.o')
        flags = ['-std=gnu11'] if source.suffix.lower() == '.c' else ['-x', 'assembler-with-cpp']
        subprocess.run([gcc, *common, *flags, '-c', str(source), '-o', str(obj)], check=True)
        return obj
    with ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_one, sources))
    response = build / 'objects.list'
    response.write_text('\n'.join('"' + str(obj).replace('\\', '/') + '"' for obj in objects))
    elf = build / 'CG2028_Enhancements.elf'
    subprocess.run([gcc, *machine, '@' + str(response), '-T' + str(ROOT / 'STM32L4S5VITX_FLASH.ld'),
                    '--specs=nosys.specs', '--specs=nano.specs', '-Wl,--gc-sections',
                    '-Wl,-Map=' + str(build / 'firmware.map'), '-Wl,--start-group', '-lc', '-lm',
                    '-Wl,--end-group', '-o', str(elf)], check=True)
    subprocess.run([str(args.toolchain / 'arm-none-eabi-size'), str(elf)], check=True)
    print(elf)

if __name__ == '__main__':
    main()
