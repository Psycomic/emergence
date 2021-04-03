#!/usr/bin/python

import glob
import os

dirs = [r'.', r'./shaders', r'../lisp']

total_lines = 0

for d in dirs:
    os.chdir(d)
    myFiles = glob.glob('*.c') + glob.glob('*.h') + glob.glob("*.glsl") + glob.glob("*.ul")
    print(myFiles)

    for path in myFiles:
        with open(path, 'r') as f:
            total_lines += f.read().count('\n')

print(total_lines)
