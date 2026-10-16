"""campaign.py - the plan `prepare-xbla-assets.ps1 -All` runs: every stage the
runtime draws an XBLA world for, and the items each needs, read from the
runtime's own tables rather than from a list kept here.

    campaign.py plan --index <source index>

prints the plan as JSON: `stages` (the launcher's stage names, in the order
of the runtime's stage table), `backgrounds` (the unique background items
those stages draw, first use first), `skydomes` (the unique dome items they
draw that the source holds) and `absent_skydomes` (dome items a stage would
ask for that the source does not hold - the game's own sky stays there).

WHERE EACH PART COMES FROM
  - stages and backgrounds: sl_wa_stage_item_path's switch
    (src/native/sl_world_assets.c), `case LEVELID_<STAGE>: ... "<item>"`.
    The enumerator's lower-case name is the launcher's stage name
    (tools/trace/sltrace/levelboot.py LEVEL_IDS), so `bunker1` draws
    `bunker`, `statue` `statuepark`, `caverns` `cavern`, `egypt` `temple`, and
    `surface2` Surface's `surface`. The launcher's own `temple` is the
    multiplayer Temple and is not in the table.
  - each stage's dome, resolved as the renderer does (sl_gfx_dl.c): a dome
    another stage's is borrowed for (s_sky_borrow, D-031), else one lent to
    the stage (s_sky_lent, D-044; both in src/platform/sl_presentation.c,
    world set "xbla"), else sl_wa_stage_sky_item_path's own answer (its
    `if (g_StageNum == LEVELID_<STAGE>) return "<item>";` lines), else the
    background item.
  - which domes the source holds: the source's own index, the file
    prepare-xbla-assets.ps1 writes (one archive path per line), where a
    dome is `...\\skydome\\<item>\\<file>.bin`.
"""
import argparse, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


class PlanError(Exception):
    pass


def _read(root, rel):
    try:
        with open(os.path.join(root, *rel.split('/')), encoding='utf-8') as f:
            return f.read()
    except OSError as exc:
        raise PlanError('%s: %s' % (rel, exc))


def _body(text, signature, rel):
    """The body of the C function whose definition line is `signature`."""
    m = re.search(re.escape(signature) + r'\s*\{(.*?)\n\}', text, re.S)
    if not m:
        raise PlanError('%s: no definition of %s' % (rel, signature))
    return m.group(1)


def stage_items(text):
    """[(stage, background item)] from sl_wa_stage_item_path's switch, in
    table order. Pure."""
    body = _body(text, 'const char *sl_wa_stage_item_path(void)', 'src/native/sl_world_assets.c')
    rows = re.findall(r'case\s+LEVELID_(\w+)\s*:[^;\n]*"([^"]+)"\s*;', body)
    if not rows:
        raise PlanError('sl_wa_stage_item_path names no stage')
    return [(s.lower(), item) for s, item in rows]


def sky_overrides(text):
    """{stage: dome item} from sl_wa_stage_sky_item_path's own answers. Pure."""
    body = _body(text, 'const char *sl_wa_stage_sky_item_path(void)', 'src/native/sl_world_assets.c')
    return {s.lower(): item for s, item in
            re.findall(r'g_StageNum\s*==\s*LEVELID_(\w+)\s*\)\s*return\s*"([^"]+)"', body)}


def sky_table(text, name, world_set='xbla'):
    """{item: dome item} from the presentation's array `name` (s_sky_borrow,
    s_sky_lent), the rows of `world_set` only. Pure."""
    m = re.search(r'\b%s\[\]\s*=\s*\{(.*?)\};' % re.escape(name), text, re.S)
    if not m:
        raise PlanError('src/platform/sl_presentation.c: no table %s' % name)
    out = {}
    for ws, item, sky in re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"', m.group(1)):
        if ws == world_set:
            out[item] = sky
    return out


def source_domes(paths):
    """The dome items the source holds: archive paths ...\\skydome\\<item>\\x.bin
    (either separator). Pure."""
    out = set()
    for p in paths:
        parts = re.split(r'[\\/]', p.strip())
        if len(parts) >= 3 and parts[-3].lower() == 'skydome' and parts[-1].lower().endswith('.bin'):
            out.add(parts[-2])
    return out


def plan(stage_rows, overrides, borrow, lent, domes, known_stages=None):
    """The -All plan from the parsed tables (stage_items, sky_overrides,
    sky_table x2), the source's domes and, when given, the launcher's stage
    names (every stage must be one). Pure."""
    stages, backgrounds, skies, absent = [], [], [], []
    for stage, item in stage_rows:
        if known_stages is not None and stage not in known_stages:
            raise PlanError('the stage table names %r, which the launcher does not know' % stage)
        if stage in stages:
            raise PlanError('the stage table names %r twice' % stage)
        stages.append(stage)
        if item not in backgrounds:
            backgrounds.append(item)
        sky = borrow.get(item) or lent.get(item) or overrides.get(stage) or item
        if sky in domes:
            if sky not in skies:
                skies.append(sky)
        elif sky not in absent:
            absent.append(sky)
    return dict(stages=stages, backgrounds=backgrounds, skydomes=skies, absent_skydomes=absent)


def known_stage_names(root=ROOT):
    text = _read(root, 'tools/trace/sltrace/levelboot.py')
    m = re.search(r'LEVEL_IDS\s*=\s*\{(.*?)\}', text, re.S)
    if not m:
        raise PlanError('tools/trace/sltrace/levelboot.py has no LEVEL_IDS')
    return set(re.findall(r'"([a-z0-9_]+)"\s*:', m.group(1)))


def campaign_plan(index_paths, root=ROOT):
    wa = _read(root, 'src/native/sl_world_assets.c')
    pres = _read(root, 'src/platform/sl_presentation.c')
    return plan(stage_items(wa), sky_overrides(wa), sky_table(pres, 's_sky_borrow'),
                sky_table(pres, 's_sky_lent'), source_domes(index_paths), known_stage_names(root))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('plan', help='print the -All plan as JSON')
    p.add_argument('--index', required=True, help='the source index (one archive path per line)')
    a = ap.parse_args(argv)
    try:
        with open(a.index, encoding='utf-8-sig') as f:
            paths = [l.rstrip('\r\n') for l in f if l.strip()]
        print(json.dumps(campaign_plan(paths)))
    except (OSError, PlanError) as exc:
        print('campaign: %s' % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
