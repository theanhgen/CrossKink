# patches/

Minimal diffs against pinned upstream, applied by `scripts/bootstrap.sh`.

Name each file `<repo>.<topic>.patch`, where `<repo>` matches a key in `UPSTREAM.lock`
(`crossink`, `freeink-sdk`, `simulator`).

Known patches this project will need:

- `freeink-sdk.geometry.patch` — 800x480 / 792x528 landscape -> 600x800 portrait.
  `libs/display/FreeInkDisplay/include/FreeInkDisplay.h:47-54`

Keep these small. If a patch grows past a few hundred lines, that upstream file has become
ours in practice — fork the repo on GitHub at that point and drop the patch. Not before.
