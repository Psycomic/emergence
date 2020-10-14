import glob
import os

os.chdir(r'.')
myFiles = glob.glob('*.c') + glob.glob('*.h')
print(myFiles)

total_lines = 0

for path in myFiles:
    with open(path, 'r') as f:
        total_lines += f.read().count('\n')

print(total_lines)
