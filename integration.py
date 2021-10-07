#!/usr/bin/env python3
import os
import subprocess

FAB_TEST_EXE = 'fab_test_program'
FAB_TEST_SRC = 'fab_test_program.c'
FABFILE = 'Fabfile'


def create_simple_program_file():
    program = '''\
#include "stdio.h"

int main() {
    puts("Hello!");
    return 0;
}
'''

    with open(FAB_TEST_SRC, mode='x') as test_program:
        test_program.write(program)


def create_simple_fabfile():
    fab = f'''\
{FAB_TEST_EXE} <- {FAB_TEST_SRC} {{
    cc -o {FAB_TEST_EXE} {FAB_TEST_SRC};
}}
'''

    with open(FABFILE, mode='x') as fabfile:
        fabfile.write(fab)


def run_fab():
    subprocess.run('./fab')


def check_executable():
    pass


def cleanup():
    for f in [FAB_TEST_EXE, FAB_TEST_SRC, FABFILE]:
        os.remove(f)


if __name__ == '__main__':
    create_simple_program_file()
    create_simple_fabfile()
    run_fab()
    check_executable()
    cleanup()
