# Scan 01 — packtool: the CLI
"""validate / build / diff / dump / flash / import / compose / overlay / card.

    packtool validate pack.json
    packtool build pack.json -o pack.patch        # region list for flashing
    packtool build --teach pack.json              # a pack that teaches (lessons unlearned)
    packtool overlay -o overlay.svg               # the honest-face keypad sheet
    packtool card pack.json -o card.svg           # the weekend quick card (writable)
    packtool diff a.json b.json
    packtool dump --port /dev/ttyUSB0 -o pack.json
    packtool dump --image eeprom.bin -o pack.json # offline
    packtool flash pack.patch --port /dev/ttyUSB0
    packtool import source.txt -o pack.json
    packtool compose events.json -o pack.json
"""

from __future__ import annotations

import argparse
import json
import sys

from . import binary
from .card import card_svg
from .compose import compose_from_file
from .importers.indyspeedway import import_text
from .model import Pack, ValidationError, validate
from .overlay import OVERLAY_SVG


def cmd_card(args) -> int:
    """Write the weekend quick card: the pack's cars as number → driver,
    pencil blanks (MY DRIVER / FAVORITES / GROUP), the stations, and the
    seven rules — the writable-strip tradition, one printable page."""
    pack = Pack.load(args.pack)
    with open(args.output, "w") as f:
        f.write(card_svg(pack))
    print(f"wrote {args.output}: {len(pack.cars)} cars, "
          f"{len(pack.stations)} stations, one page")
    return 0


def cmd_overlay(args) -> int:
    """Write the printable keypad overlay: the honest face of the radio —
    plain digits where the moulded keypad prints ham jargon (BAND, VOX, ...),
    HOLD on the PTT, MUTE/GROUP on the side keys, and the hold-glyph legend."""
    with open(args.output, "w") as f:
        f.write(OVERLAY_SVG)
    print(f"wrote {args.output}: the honest-face overlay sheet")
    return 0


def cmd_validate(args) -> int:
    pack = Pack.load(args.pack)
    try:
        warnings = validate(pack)
    except ValidationError as e:
        print(f"INVALID: {e}")
        return 1
    for w in warnings:
        print(f"warning: {w.text}")
    print(f"OK: {len(pack.cars)} cars, {len(pack.stations)} stations, "
          f"{len(pack.flattened())} entries after flattening")
    return 0


def cmd_build(args) -> int:
    pack = Pack.load(args.pack)
    try:
        warnings = validate(pack)
    except ValidationError as e:
        print(f"INVALID: {e}")
        return 1
    for w in warnings:
        print(f"warning: {w.text}")
    if args.teach:
        pack.meta["lessons"] = 0        # a teaching pack: the radio explains itself
    regions = binary.build(pack)
    patch = {"meta": pack.meta,
             "regions": [{"addr": r.addr, "data": r.data.hex()} for r in regions]}
    with open(args.output, "w") as f:
        json.dump(patch, f, indent=1)
        f.write("\n")
    print(f"wrote {args.output}: {len(regions)} regions, "
          f"{sum(len(r.data) for r in regions)} bytes")
    return 0


def cmd_diff(args) -> int:
    a, b = Pack.load(args.a), Pack.load(args.b)
    changed = 0
    an = {c.number: c for c in a.flattened()}
    bn = {c.number: c for c in b.flattened()}
    for num in sorted(set(an) | set(bn)):
        ca, cb = an.get(num), bn.get(num)
        if ca is None or cb is None:
            print(f"- car {num}: {'removed' if ca else 'added'}")
            changed += 1
        elif ca.freqs != cb.freqs or ca.tone != cb.tone:
            print(f"~ car {num}: {ca.freqs}/{ca.tone} -> {cb.freqs}/{cb.tone}")
            changed += 1
    sa = {(s.name, s.freq or s.fm): s for s in a.stations}
    sb = {(s.name, s.freq or s.fm): s for s in b.stations}
    for key in sorted(set(sa) | set(sb)):
        if key not in sa or key not in sb:
            print(f"- station {key[0]}: {'removed' if key in sa else 'added'}")
            changed += 1
    al, bl = set(a.lockouts), set(b.lockouts)
    for n in sorted(al ^ bl):
        print(f"~ lockout {n}: {'removed' if n in al else 'added'}")
        changed += 1
    print(f"{'identical' if changed == 0 else f'{changed} differences'}")
    return 0 if changed == 0 else 1


def cmd_dump(args) -> int:
    if args.image:
        with open(args.image, "rb") as f:
            image = f.read()
    else:
        from .uart import Radio
        radio = Radio(args.port)
        try:
            challenge = radio.identify()
            if not radio.authenticate(challenge):
                print("auth failed (locked radio?)")
                return 1
            image = radio.read_full()
        finally:
            radio.close()
    try:
        pack = binary.parse(image)
    except ValueError as e:
        print(f"dump: {e}")
        return 1
    pack.save(args.output)
    print(f"wrote {args.output}: {len(pack.cars)} cars, {len(pack.stations)} stations")
    return 0


def cmd_flash(args) -> int:
    from .uart import Radio
    with open(args.patch) as f:
        patch = json.load(f)
    regions = [binary.Region(r["addr"], bytes.fromhex(r["data"])) for r in patch["regions"]]
    radio = Radio(args.port)
    try:
        challenge = radio.identify()
        if not radio.authenticate(challenge):
            print("auth failed (locked radio?)")
            return 1
        radio.write_regions(regions)
    finally:
        radio.close()
    print(f"flashed {len(regions)} regions")
    return 0


def cmd_import(args) -> int:
    with open(args.source) as f:
        text = f.read()
    pack = import_text(text)
    try:
        warnings = validate(pack)
    except ValidationError as e:
        print(f"INVALID: {e}")
        return 1
    for w in warnings:
        print(f"warning: {w.text}")
    pack.save(args.output)
    print(f"wrote {args.output}: {len(pack.cars)} cars, {len(pack.stations)} stations")
    return 0


def cmd_compose(args) -> int:
    pack, report = compose_from_file(
        args.events, args.library,
        series=args.series, track=args.track, session=args.session,
        season=args.season, date=args.date, author=args.author)
    for w in report.warnings:
        print(f"warning: {w}")
    for t in report.trims:
        print(f"trim: {t}")
    pack.save(args.output)
    print(f"wrote {args.output}: {len(pack.cars)} cars, {len(pack.stations)} stations, "
          f"venues {pack.meta.get('venues')}")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="packtool", description="Scan 01 pack toolchain")
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("validate", help="validate a pack JSON (§6)")
    sp.add_argument("pack")
    sp.set_defaults(fn=cmd_validate)

    sp = sub.add_parser("build", help="build the EEPROM region list")
    sp.add_argument("pack")
    sp.add_argument("-o", "--output", default="pack.patch")
    sp.add_argument("--teach", action="store_true",
                    help="leave the lessons unlearned: the radio teaches itself")
    sp.set_defaults(fn=cmd_build)

    sp = sub.add_parser("overlay", help="write the printable keypad overlay (SVG)")
    sp.add_argument("-o", "--output", default="overlay.svg")
    sp.set_defaults(fn=cmd_overlay)

    sp = sub.add_parser("card", help="write the weekend quick card (SVG)")
    sp.add_argument("pack")
    sp.add_argument("-o", "--output", default="card.svg")
    sp.set_defaults(fn=cmd_card)

    sp = sub.add_parser("diff", help="diff two pack JSONs")
    sp.add_argument("a")
    sp.add_argument("b")
    sp.set_defaults(fn=cmd_diff)

    sp = sub.add_parser("dump", help="dump the radio's pack (port or image)")
    sp.add_argument("--port", default=None)
    sp.add_argument("--image", default=None)
    sp.add_argument("-o", "--output", default="pack.json")
    sp.set_defaults(fn=cmd_dump)

    sp = sub.add_parser("flash", help="flash a patch to the radio")
    sp.add_argument("patch")
    sp.add_argument("--port", required=True)
    sp.set_defaults(fn=cmd_flash)

    sp = sub.add_parser("import", help="import a scanner-frequencies text extract")
    sp.add_argument("source")
    sp.add_argument("-o", "--output", default="pack.json")
    sp.set_defaults(fn=cmd_import)

    sp = sub.add_parser("compose", help="assemble a weekend pack from events.json")
    sp.add_argument("events")
    sp.add_argument("--library", action="append", default=[])
    sp.add_argument("-o", "--output", default="pack.json")
    sp.add_argument("--series", default="")
    sp.add_argument("--track", default="")
    sp.add_argument("--session", default="")
    sp.add_argument("--season", type=int, default=0)
    sp.add_argument("--date", default="")
    sp.add_argument("--author", default="")
    sp.set_defaults(fn=cmd_compose)

    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
