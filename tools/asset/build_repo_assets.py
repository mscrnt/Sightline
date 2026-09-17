#!/usr/bin/env python3
r"""Build every committed asset-override SOURCE package into a .slmodel.

WHY THIS EXISTS
    data/asset-overrides/ ships MODELS, and until now the only way to get one
    was to run the importer by hand. That is fine for a developer and wrong
    for a player: an override that needs a manual import step is an override
    most users never see. This step closes that gap - build.ps1 runs it, and
    the model is there on the next launch with nothing else typed.

SOURCE IS COMMITTED, THE BINARY IS BUILT
    The .slmodel is a build output. It is written into the build tree, NOT
    into data/asset-overrides/, and it is not tracked. What is tracked is the
    glTF the author wrote, which is reviewable in a way a 260 KB binary is
    not, and which the tests can re-derive from at any time.

WHERE IT WRITES, AND WHY THERE
    build\win32\data\asset-overrides\<relpath>

    That is not a new location invented here. It is the SECOND repository
    candidate the runtime already searches - "a packaged layout:
    data\asset-overrides sitting beside the executable", aov_repo_dirs() in
    src/native/sl_asset_override.c:209. The full ladder the runtime walks is

        install directory  ->  <repo>/data/asset-overrides
                           ->  <exedir>/data/asset-overrides
                           ->  the original asset

    so a player's own import still wins (candidate 0), a hand-committed
    binary still wins over a build output, and shipping the build directory
    ships the models with it. Writing into <repo>/data/asset-overrides
    instead would put an untracked binary inside a tracked directory, where
    it is one `git clean` from vanishing and one `git add -A` from being
    committed by accident.

THE NAMING CONVENTION IS THE MANIFEST
    data/asset-overrides/source/boot/goldeneye_logo/  ->  boot.goldeneye_logo

    A directory two levels under source/ names an asset id by joining its two
    path components with a dot, and that id must already exist in
    gltf_import.ASSET_IDS. There is no separate list to keep in step, and a
    directory naming an id the runtime does not have is an ERROR rather than
    a silent skip - a typo there would otherwise build nothing and say
    nothing.

IT IS --repo, NOT A PRIVATE PATH
    Conversion goes through gltf_import's ordinary --repo mode, so a source
    package that would not be acceptable to commit does not become acceptable
    by being built. Embedded pixels are refused unless the glTF declares them
    as the author's own (extras.sl_authored); ROM-derived textures are turned
    into references before that declaration is ever read.

ABSENCE IS NOT FAILURE
    No source tree, or no packages in it, prints one line and exits 0. A
    build that carries no asset sources is a normal build.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import gltf_import as gi  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIR = REPO_ROOT / "data" / "asset-overrides" / "source"
DEFAULT_OUT = REPO_ROOT / "build" / "win32" / "data" / "asset-overrides"


def find_source(pkg: Path):
    """The one glTF in a package directory.

    Ambiguity is an error, not a first-match: a package with two glTFs would
    otherwise build whichever one sorted first, and change silently the day
    someone added a second export.
    """
    cands = sorted(list(pkg.glob("*.gltf")) + list(pkg.glob("*.glb")))
    if not cands:
        return None, "no .gltf or .glb in %s" % pkg
    if len(cands) > 1:
        return None, ("%s holds %d model files (%s); a package must hold one"
                      % (pkg, len(cands), ", ".join(c.name for c in cands)))
    return cands[0], None


def inputs_of(pkg: Path):
    """Every file that can change the output - the glTF, its buffers and its
    textures. Used for the up-to-date check, so an edited .bin or .png
    rebuilds even though the .gltf itself did not move."""
    return [p for p in pkg.rglob("*") if p.is_file()]


def newest(paths):
    return max((p.stat().st_mtime_ns for p in paths), default=0)


def texture_refs_of(pkg: Path):
    """Optional per-package texture-reference declarations.

    WHY A PACKAGE NEEDS TO SAY THIS. The importer recognises a game texture by
    CONTENT, which works only while the package actually carries those pixels -
    and a package that carries them cannot be committed, because they are the
    game's. So the one asset whose only texture IS a game texture could be
    built or committed, never both.

    --texture-ref already solves exactly that: "declare that an image IS a game
    texture, when it has been re-saved or edited and so is not recognised by
    content". It was reachable only from the command line, so this reads the
    same declaration out of the package and hands it to the SAME option. No
    second reference system, and nothing here decides what a texture is - the
    importer still validates the identifier against its own registry and still
    refuses one it does not know.

    OPTIONAL, AND SILENT WHEN ABSENT. No metadata.json, no texture_refs key, or
    an empty one, all return {} and the package builds exactly as before. Only
    goldeneye_logo has a metadata.json at all today and it declares no refs, so
    every existing package is unaffected.

    A MALFORMED declaration is an ERROR rather than a shrug: a typo that
    silently built a model with an embedded texture, or with no texture, is the
    failure this whole path exists to prevent.
    """
    meta = pkg / "metadata.json"
    if not meta.is_file():
        return {}
    try:
        doc = json.loads(meta.read_text(encoding="utf-8"))
    except ValueError as exc:
        raise SystemExit("error: %s is not valid JSON: %s" % (meta, exc))
    refs = doc.get("texture_refs", {})
    if not refs:
        return {}
    if not isinstance(refs, dict):
        raise SystemExit("error: %s: texture_refs must be an object mapping "
                         "image name -> game texture identifier" % meta)
    for k, v in refs.items():
        if not isinstance(k, str) or not isinstance(v, str) or not k or not v:
            raise SystemExit("error: %s: texture_refs entries must be "
                             "non-empty strings, got %r: %r" % (meta, k, v))
    return refs


def packages():
    """(asset_id, package_dir) for every source package, sorted by id."""
    out = []
    if not SOURCE_DIR.is_dir():
        return out
    for group in sorted(p for p in SOURCE_DIR.iterdir() if p.is_dir()):
        for pkg in sorted(p for p in group.iterdir() if p.is_dir()):
            out.append(("%s.%s" % (group.name, pkg.name), pkg))
    return out


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    out_root = Path(argv[0]) if argv else DEFAULT_OUT
    force = "--force" in argv

    pkgs = packages()
    if not pkgs:
        print("asset overrides: no source packages under "
              "data/asset-overrides/source - nothing to build")
        return 0

    built = skipped = 0
    for asset, pkg in pkgs:
        if asset not in gi.ASSET_IDS:
            print("error: %s names asset id %r, which the runtime does not "
                  "have. Known ids: %s"
                  % (pkg, asset, ", ".join(sorted(gi.ASSET_IDS))),
                  file=sys.stderr)
            return 1
        src, err = find_source(pkg)
        if src is None:
            print("error: %s" % err, file=sys.stderr)
            return 1

        out = out_root / gi.ASSET_IDS[asset]
        if not force and out.is_file() and \
                out.stat().st_mtime_ns >= newest(inputs_of(pkg)):
            print("asset overrides: %s is up to date" % asset)
            skipped += 1
            continue

        # The ordinary importer, in the ordinary --repo mode. Calling main()
        # rather than convert() keeps this step honest: it gets the same
        # checks, the same bounds report and the same refusals a person typing
        # the command would get, and there is no second code path to drift.
        argv = ["--repo", "--asset", asset, "--input", str(src),
                "--output", str(out)]
        for image, ident in sorted(texture_refs_of(pkg).items()):
            argv += ["--texture-ref", "%s=%s" % (image, ident)]
        rc = gi.main(argv)
        if rc != 0:
            print("error: building %s from %s failed (rc %d)"
                  % (asset, src, rc), file=sys.stderr)
            return rc
        built += 1

    print("asset overrides: %d built, %d up to date -> %s"
          % (built, skipped, out_root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
