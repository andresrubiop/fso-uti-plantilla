#!/usr/bin/env python3
"""Does the C inference (tinyllm.c) compute the same as the NumPy reference model (train.py)?
Generates N new requests, solves them with both and counts the identical answers.
    python3 compare.py ../../bin/td8/tinyllm [N]
"""
import random
import subprocess
import sys

import dataset
import train

exe, n = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 200
p = train.load()
r = random.Random(777)
same = 0
for i in range(n):
    q, _ = dataset.example(r)
    py = train.generate(p, q)
    c = subprocess.run([exe, '-m', str(train.WEIGHTS), q], capture_output=True, text=True).stdout.rstrip('\n')
    if py == c:
        same += 1
    elif same + 3 > i:          # show the first differences
        print(f'distinto: {q!r}\n  numpy: {py!r}\n  C:     {c!r}')
print(f'{same}/{n} respuestas idénticas entre NumPy y C')
sys.exit(0 if same == n else 1)
