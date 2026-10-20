"""Copyright (c) 2026 mio. Reuse the M10a2 launcher while exposing the CPU hardware RNG.

Only the QEMU command for this child process changes. No system files or images
are edited. Default layout: SandCore-main/SandBrowser beside SandCore-M10a2.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True


def enable_rng(command):
    result = list(command)
    index = result.index('-cpu') + 1
    fields = result[index].split(',')
    fields = [field for field in fields if field not in
              ('+rdrand', '-rdrand', 'rdrand=on', 'rdrand=off')]
    result[index] = ','.join(fields + ['+rdrand'])
    return result


def main():
    parser = argparse.ArgumentParser(description='Start SandCore M10a2 with hardware RNG for HTTPS')
    parser.add_argument('--system', type=Path,
                        default=Path(__file__).resolve().parents[2].parent / 'SandCore-M10a2',
                        help='directory containing the system run.py and both disk images')
    args, remaining = parser.parse_known_args()
    launcher = args.system.resolve() / 'run.py'
    if not launcher.is_file():
        raise RuntimeError('System launcher not found: ' + str(launcher) +
                           '. Use --system to choose the extracted SandCore M10a2 folder.')
    spec = importlib.util.spec_from_file_location('sandbrowser_m10a2_launcher', launcher)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original_call, original_args = subprocess.call, sys.argv

    def launch(command, *positional, **keywords):
        command = enable_rng(command)
        print('HTTPS CPU profile: ' + command[command.index('-cpu') + 1], flush=True)
        return original_call(command, *positional, **keywords)

    subprocess.call = launch
    sys.argv = [str(launcher), *remaining]
    try:
        if '--check' in remaining:
            print('HTTPS launcher: RDRAND will be exposed to the guest CPU.', flush=True)
        return module.main()
    finally:
        subprocess.call = original_call
        sys.argv = original_args


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
