#!/usr/bin/env python3
import os
import subprocess

FAB_TEST_EXE = 'fab_test_program'
FAB_TEST_SRC = 'fab_test_program.c'
FABFILE = 'Fabfile'


def create_fabfile_with_deps():
    fab = '''\
foo <- bar {
    echo 3;
}

bar <- baz {
    echo 2;
}

baz <- qux {
    echo 1;
}
'''
    with open(FABFILE, mode='x') as fabfile:
        fabfile.write(fab)


def run_fab(target):
    return subprocess.run(f'./fab {target}'.split(), capture_output=True)


def check_output(actual):
    assert '1\n2\n3\n' == actual.decode()


def cleanup():
    for f in [FABFILE]:
        os.remove(f)


if __name__ == '__main__':
    create_fabfile_with_deps()
    handle = run_fab('foo')
    check_output(handle.stdout)
    cleanup()
