#!/usr/bin/env python3
import pathlib,sys
root=pathlib.Path(__file__).resolve().parents[2]
with pathlib.Path(sys.argv[1]).open('w') as out:
    for name in ('recording-start','recording-limit'):
        data=(root/'assets'/f'{name}.wav').read_bytes()
        out.write(f'static const unsigned char {name.replace("-","_")}[] = {{\n')
        for start in range(0,len(data),20): out.write(','.join(str(n) for n in data[start:start+20])+',\n')
        out.write('};\n')

with pathlib.Path(sys.argv[1]).open('a') as out:
    data=(root/'assets'/'keyscribe-menu.png').read_bytes()
    out.write('static const unsigned char keyscribe_menu[] = {\n')
    for start in range(0,len(data),20): out.write(','.join(str(n) for n in data[start:start+20])+',\n')
    out.write('};\n')
