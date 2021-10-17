#!/usr/bin/env python3
import subprocess

COL = 78


def report(name, status):
    dots = '.' * (COL - len(name) - len(status))
    print(f'{name}{dots}{status}')


class Manifest:
    def __init__(self, mft_name):
        with open(f'manifests/{mft_name}.mft') as mft:
            self.name = mft_name
            self.target, self.stream = mft.readlines()[0].rstrip().split(',')

        with open(f'output/{mft_name}.{self.stream}') as exp:
            self.expected = ''.join(exp.readlines()).rstrip()

    def is_stderr(self):
        return 'stderr' == self.stream

    def is_stdout(self):
        assert not self.is_stderr()
        return 'stdout' == self.stream


def run(mft):
    handle = subprocess.run(
        f'../fab -f fabfiles/{mft.name}.fab {mft.target}'.split(),
        capture_output=True)

    if mft.is_stdout():
        return handle.stdout.decode().rstrip()
    else:
        assert mft.is_stderr()
        return handle.stderr.decode().rstrip()


def check(name):
    mft = Manifest(name)
    actual = run(mft)

    if actual == mft.expected:
        report(name, 'ok')
    else:
        report(name, 'fail')


if __name__ == '__main__':
    check('advent')
    check('chain_dependency')
    check('dag')
    check('default_rule')
    check('macro_reference_macro')
    check('macros')
    check('multiple_actions_in_action_block')
    check('target_alias')
