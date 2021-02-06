#!/usr/bin/python

import glob
import os

dirs = [r'.', r'./shaders']

total_lines = 0

for d in dirs:
    os.chdir(d)
    myFiles = glob.glob('*.c') + glob.glob('*.h') + glob.glob("*.glsl")
    print(myFiles)

    for path in myFiles:
        with open(path, 'r') as f:
            total_lines += f.read().count('\n')

print(total_lines)
