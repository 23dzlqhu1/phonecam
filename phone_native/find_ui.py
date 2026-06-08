import re
import sys
import io

with open(sys.argv[1], encoding='utf-8') as f:
    content = f.read()

# Use UTF-8 stdout
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

# Find all nodes with text and bounds
for m in re.finditer(r'text="([^"]*)"[^>]*?bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', content):
    t = m.group(1)
    x1, y1, x2, y2 = int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5))
    if t:
        cx, cy = (x1+x2)//2, (y1+y2)//2
        print(f'  "{t[:40]}" center=({cx},{cy}) bounds=[{x1},{y1}][{x2},{y2}]')
