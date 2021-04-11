#!/usr/bin/python

import glob
import os

dirs = [r'.', r'./shaders', r'../lisp']

total_lines = 0

files_lines = []

for d in dirs:
    os.chdir(d)
    myFiles = glob.glob('*.c') + glob.glob('*.h') + glob.glob("*.glsl") + glob.glob("*.ul")

    for path in myFiles:
        with open(path, 'r') as f:
            lines = f.read().count('\n')
            files_lines.append((path, lines))

files_lines.sort(key=lambda a: a[1])

for f, l in files_lines:
    print(f"{f}: {l}")

print(f"Total: {sum(map(lambda x: x[1], files_lines))}")
