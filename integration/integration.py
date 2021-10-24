#!/usr/bin/env python3
import subprocess

COL = 78


def report(name, status):
    dots = '.' * (COL - len(name) - len(status))
    print(f'{name}{dots}{status}')


class Manifest:
    def __init__(self, name, fd):
        self.fd = fd
        self.name = name
        with open(f'output/{name}.{fd}') as exp:
            self.expected = ''.join(exp.readlines()).rstrip()

    def is_stderr(self):
        return 'stderr' == self.fd

    def is_stdout(self):
        return 'stdout' == self.fd


def run(mft):
    handle = subprocess.run(f'../fab -f fabfiles/{mft.name}.fab'.split(),
                            capture_output=True)

    if mft.is_stdout():
        return handle.stdout.decode().rstrip()
    else:
        assert mft.is_stderr()
        return handle.stderr.decode().rstrip()


def check(name, fd):
    mft = Manifest(name, fd)
    actual = run(mft)

    if actual == mft.expected:
        report(name, 'ok')
        return 1
    else:
        report(name, 'fail')
        return 0


if __name__ == '__main__':
    with open('manifest') as mft:
        total = 0
        passed = 0
        for line in mft.readlines():
            total += 1
            passed += check(*line.rstrip().split(','))

        print(f'\n{passed}/{total} tests passed.')
