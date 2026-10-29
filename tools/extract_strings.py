"""List every UI string the app passes to L(ru, en): prints the English text of each pair."""
import re, sys, pathlib

root = pathlib.Path(__file__).resolve().parent.parent / "Source" / "app"
BS = chr(92)
one = '"(?:[^"' + BS + BS + ']|' + BS + BS + '.)*"'
lit = '(?:' + one + r'\s*)+'
pat = re.compile(r'\bL\s*\(\s*(' + lit + r')\s*,\s*(' + lit + r')\)', re.S)
part = re.compile('"((?:[^"' + BS + BS + ']|' + BS + BS + '.)*)"')


def join(s):
    return "".join(part.findall(s)).replace(BS + '"', '"')


def pairs():
    out = []
    for f in sorted(root.glob("*.*")):
        if f.suffix not in (".cpp", ".h"):
            continue
        for m in pat.finditer(f.read_text(encoding="utf-8")):
            out.append((f.name, join(m.group(1)), join(m.group(2))))
    return out


if __name__ == "__main__":
    seen = {}
    for f, ru, en in pairs():
        seen.setdefault(en, (f, ru))
    for en, (f, ru) in seen.items():
        print(f"{f}\t{en}")
    print(len(seen), "unique", file=sys.stderr)
