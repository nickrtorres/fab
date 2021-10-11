#!/usr/bin/env python3
import subprocess


def run(manifest):
    with open(f'manifests/{manifest}.mft') as mft:
        target, stream = mft.readlines()[0].split(',')

        handle = subprocess.run(
            f'../fab -f fabfiles/{manifest}.fab {target}'.split(),
            capture_output=True)

        if stream.rstrip() == 'stdout':
            return handle.stdout.decode()
        else:
            assert stream == 'stderr'
            return handle.stderr.decode()


def check(manifest):
    actual = run(manifest)
    with open(f'output/{manifest}') as out:
        expected = ''.join(out.readlines())

    if actual != expected:
        print('error expected:')
        print(expected)
        print('got:')
        print(actual)


if __name__ == '__main__':
    check('BasicDep')
