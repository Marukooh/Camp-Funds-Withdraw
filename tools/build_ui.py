#!/usr/bin/env python3
"""Build the Camp Funds Withdraw UI overrides from the game's original files.

Usage: build_ui.py <original_dir> <out_files_dir>

<original_dir> must contain the original (decrypted) files:
  warehouseview2_game.uibinary, warehouseview2.html, inputmap_common.xml
(extract them with ../tools/cddecrypt.py from shard 0012).

Writes into <out_files_dir>/0012/ui/... the DMM layout:
  bin__/warehouseview2_game.uibinary   - DonateToCamp keyguide subtree cloned as WithdrawCampFunds
  xml/gamemain/play/warehouseview2.html - same change in source form
  inputmap_common.xml                   - new <Input Name="WithdrawCampFunds"> in WareHousePanel

uibinary notes: sequential tree, strings are u32 length + bytes, lists are
u8 flag (0 = absent, 1 = present) + u32 count + items. No absolute offsets, so a subtree can be
copied and strings can change length.
"""
import struct
import sys
from pathlib import Path

KEYMAP = 'WithdrawCampFunds'
CSS_CLASS = 'cpp-keyguide-withdrawcampfunds'
SELECTOR_ATTR = 'selector-withdraw-campfunds-keyguide'
WIDGET_ID = 'WareHouseKeyguideWithdrawCampFunds'
LABEL_LOCSTR = 'UI_Bank_WithdrawMoney'
KEY_KEYBOARD = 't'
KEY_PAD = 'buttonRB'


def lp(s):
    b = s.encode()
    return struct.pack('<I', len(b)) + b


def read_strs(d, pos, n):
    """Parse n length-prefixed strings at pos; return (list, end) or None."""
    out = []
    for _ in range(n):
        if pos + 4 > len(d):
            return None
        ln = struct.unpack_from('<I', d, pos)[0]
        if ln > 4096 or pos + 4 + ln > len(d):
            return None
        out.append(d[pos + 4:pos + 4 + ln].decode('utf-8', 'replace'))
        pos += 4 + ln
    return out, pos


def find_class_list(d, cls, start=0):
    """Find the class list header (01 u32 n) of the node whose class list contains `cls`."""
    target = d.find(lp(cls), start)
    if target < 0:
        raise SystemExit(f'class {cls} not found')
    for p in range(target - 1, max(0, target - 400), -1):
        if d[p] != 1:
            continue
        n = struct.unpack_from('<I', d, p + 1)[0]
        if not 1 <= n <= 32:
            continue
        r = read_strs(d, p + 5, n)
        if r and cls in r[0] and r[0] == sorted(r[0]):
            return p, r[1]
    raise SystemExit(f'class list header for {cls} not found')


def replace_lp(blob, old, new, expect=None):
    cnt = blob.count(lp(old))
    if expect is not None and cnt != expect:
        raise SystemExit(f'{old!r}: expected {expect} occurrences, found {cnt}')
    return blob.replace(lp(old), lp(new)), cnt


def patch_uibinary(d):
    d = bytearray(d)
    don_start, _ = find_class_list(d, 'cpp-keyguide-factiondonation')
    nxt_start, _ = find_class_list(d, 'cpp-keyguide-showDetail', don_start)
    # Parent (FunctionalKeyGuideWrap) children list header right before the first child.
    if d[don_start - 5] != 1:
        raise SystemExit('parent children list not where expected')
    kids = struct.unpack_from('<I', d, don_start - 4)[0]
    span = bytes(d[don_start:nxt_start])
    if lp('WareHouseKeyguideFactionDonation') not in span or lp('DonateToCamp') not in span:
        raise SystemExit('donation keyguide span looks wrong')
    clone = span
    clone, _ = replace_lp(clone, 'cpp-keyguide-factiondonation', CSS_CLASS, 1)
    clone, _ = replace_lp(clone, 'DonateToCamp', KEYMAP, 1)
    clone, n_loc = replace_lp(clone, 'UI_WareHouse_KeyGuideDonateToCamp', LABEL_LOCSTR)
    clone, _ = replace_lp(clone, 'WareHouseKeyguideFactionDonation', WIDGET_ID, 1)
    if n_loc < 1:
        raise SystemExit('label localstring not found in clone')
    d[nxt_start:nxt_start] = clone
    struct.pack_into('<I', d, don_start - 4, kids + 1)

    # Body attribute: selector for the new keyguide, inserted after the donation selector pair.
    pair = lp('selector-factiondonation-keyguide') + lp('.cpp-keyguide-factiondonation')
    ppos = d.find(pair)
    if ppos < 0 or d.find(pair, ppos + 1) >= 0:
        raise SystemExit('body selector pair not unique')
    header = None
    for p in range(ppos - 1, max(0, ppos - 20000), -1):
        if d[p] != 1:
            continue
        n = struct.unpack_from('<I', d, p + 1)[0]
        if not 1 <= n <= 300:
            continue
        r = read_strs(d, p + 5, 2 * n)
        if not r:
            continue
        # The pair must start on a key boundary inside this list.
        pos, ok = p + 5, False
        for _ in range(n):
            if pos == ppos:
                ok = True
            ln = struct.unpack_from('<I', d, pos)[0]
            pos += 4 + ln
            ln = struct.unpack_from('<I', d, pos)[0]
            pos += 4 + ln
        if ok and pos == r[1]:
            header = (p, n)
            break
    if not header:
        raise SystemExit('body attribute list header not found')
    ins = ppos + len(pair)
    d[ins:ins] = lp(SELECTOR_ATTR) + lp('.' + CSS_CLASS)
    struct.pack_into('<I', d, header[0] + 1, header[1] + 1)
    return bytes(d), kids, header[1]


def patch_html(text):
    anchor = '\t\tselector-factiondonation-keyguide=".cpp-keyguide-factiondonation"\n'
    if anchor not in text:
        anchor = anchor.replace('\n', '\r\n')
    nl = '\r\n' if anchor.endswith('\r\n') else '\n'
    if anchor not in text:
        raise SystemExit('html body anchor not found')
    text = text.replace(anchor, anchor + f'\t\t{SELECTOR_ATTR}=".{CSS_CLASS}"{nl}', 1)
    start = text.find('<widget id="WareHouseKeyguideFactionDonation"')
    end = text.find('</widget>', start) + len('</widget>')
    if start < 0 or end < len('</widget>'):
        raise SystemExit('html donation widget not found')
    w = text[start:end]
    w = (w.replace('WareHouseKeyguideFactionDonation', WIDGET_ID)
          .replace('cpp-keyguide-factiondonation', CSS_CLASS)
          .replace('"DonateToCamp"', f'"{KEYMAP}"')
          .replace('UI_WareHouse_KeyGuideDonateToCamp', LABEL_LOCSTR))
    line_start = text.rfind('\n', 0, start) + 1
    indent = text[line_start:start]
    return text[:end] + nl + nl + indent + w + text[end:]


def patch_inputmap(text):
    anchor = '\t<Input Name="DonateToCamp">'
    pos = text.find(anchor, text.find('<InputGroup Name="WareHousePanel"'))
    if pos < 0:
        raise SystemExit('inputmap anchor not found')
    nl = '\r\n' if '\r\n' in text[pos:pos + 200] else '\n'
    block = (f'\t<Input Name="{KEYMAP}">{nl}'
             f'\t\t<KeyboardMouse Key="{KEY_KEYBOARD}" Method="downonce"/>{nl}'
             f'\t\t<KeyboardMouse Key="{KEY_KEYBOARD}" Method="release"/>{nl}'
             f'\t\t<GamePad Key="{KEY_PAD}" Method="downonce"/>{nl}'
             f'\t\t<GamePad Key="{KEY_PAD}" Method="release"/>{nl}'
             f'\t</>{nl}')
    return text[:pos] + block + text[pos:]


def rw_text(path):
    raw = path.read_bytes()
    bom = raw.startswith(b'\xef\xbb\xbf')
    return raw[3:].decode('utf-8') if bom else raw.decode('utf-8'), bom


def wr_text(path, text, bom):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes((b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8'))


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2]) / '0012' / 'ui'
    ub, kids, nattrs = patch_uibinary((src / 'warehouseview2_game.uibinary').read_bytes())
    (out / 'bin__').mkdir(parents=True, exist_ok=True)
    (out / 'bin__' / 'warehouseview2_game.uibinary').write_bytes(ub)
    html, bom = rw_text(src / 'warehouseview2.html')
    wr_text(out / 'xml' / 'gamemain' / 'play' / 'warehouseview2.html', patch_html(html), bom)
    im, bom = rw_text(src / 'inputmap_common.xml')
    wr_text(out / 'inputmap_common.xml', patch_inputmap(im), bom)
    print(f'uibinary: keyguide wrap children {kids} -> {kids + 1}, body attrs {nattrs} -> {nattrs + 1}, '
          f'size {len(ub)}')


if __name__ == '__main__':
    main()
