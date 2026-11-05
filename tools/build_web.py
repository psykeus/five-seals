#!/usr/bin/env python3
"""Build the browser-playable page: embeds the ROM and the jsnes emulator into one HTML file.
Usage: python3 tools/build_web.py <path to jsnes.min.js> <output.html>
"""
import base64
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    jsnes_path, out = sys.argv[1], sys.argv[2]
    t = open(os.path.join(ROOT, 'tools', 'web_template.html')).read()
    js = re.sub(r'//# sourceMappingURL=.*', '', open(jsnes_path).read())
    assert '</script' not in js.lower()
    rom = base64.b64encode(open(os.path.join(ROOT, 'build', 'fiveseals.nes'), 'rb').read()).decode()
    t = t.replace('/*__JSNES__*/', js).replace('/*__ROM__*/', rom)
    open(out, 'w').write(t)
    print('wrote %s (%d bytes)' % (out, len(t)))


if __name__ == '__main__':
    main()
