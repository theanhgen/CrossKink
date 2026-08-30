#!/usr/bin/env bash
# Fetch the candidate corpus. URLs are the ones in
# lib/EpdFont/scripts/sd-fonts.yaml (regular style only); the four already
# under builtinFonts/source/ are copied from there.
#
# Idempotent: skips anything already present. Safe to re-run.
set -uo pipefail
cd "$(dirname "$0")"
mkdir -p fonts

# Two layouts carry the CrossInk tree in different places: the CrossKink fork
# has it in-tree, the standalone overlay clones it into vendor/. Try both.
SRC=""
for c in ../lib/EpdFont/builtinFonts/source \
         ../vendor/crossink/lib/EpdFont/builtinFonts/source ; do
  [ -d "$c" ] && { SRC="$c"; break; }
done

get() {  # get <outfile> <url>
  [ -s "fonts/$1" ] && { echo "have $1"; return 0; }
  if curl -sfL --max-time 60 -o "fonts/$1" "$2"; then echo "ok   $1"
  else echo "FAIL $1  $2" >&2; rm -f "fonts/$1"; return 1; fi
}

copy() { # copy <outfile> <relative path under builtinFonts/source>
  [ -s "fonts/$1" ] && { echo "have $1"; return 0; }
  if [ -f "$SRC/$2" ]; then cp "$SRC/$2" "fonts/$1"; echo "ok   $1 (vendored)"
  else echo "SKIP $1 — CrossInk sources not found (looked in ../lib and ../vendor/crossink)" >&2; fi
}

# --- already in the tree (bootstrap.sh puts them there) ---
copy Bitter.ttf      Bitter/Bitter-Regular.ttf
copy Inter.ttf       Inter/Inter-Regular.ttf
copy LexendDeca.ttf  LexendDeca/LexendDeca-Regular.ttf
copy NotoSans.ttf    NotoSans/NotoSans-Regular.ttf

# --- serif ---
get ChareInk.ttf         "https://raw.githubusercontent.com/uxjulia/crossink-fonts/main/fonts/ChareInk7/ChareInk7-Regular.ttf"
get GentiumBookPlus.ttf  "https://raw.githubusercontent.com/google/fonts/main/ofl/gentiumbookplus/GentiumBookPlus-Regular.ttf"
get IBMPlexSerif.ttf     "https://raw.githubusercontent.com/google/fonts/main/ofl/ibmplexserif/IBMPlexSerif-Regular.ttf"
get Literata.ttf         "https://raw.githubusercontent.com/googlefonts/literata/main/fonts/ttf/Literata-Regular.ttf"
get Lora.ttf             "https://raw.githubusercontent.com/cyrealtype/Lora-Cyrillic/main/fonts/ttf/Lora-Regular.ttf"
get Merriweather.ttf     "https://raw.githubusercontent.com/SorkinType/Merriweather/master/fonts/ttf/Merriweather-Regular.ttf"
get NotoSerif.ttf        "https://raw.githubusercontent.com/notofonts/NotoSerif/main/fonts/ttf/unhinted/instance_ttf/NotoSerif-Regular.ttf"
get SourceSerif4.ttf     "https://raw.githubusercontent.com/adobe-fonts/source-serif/release/TTF/SourceSerif4-Regular.ttf"
get Tinos.ttf            "https://raw.githubusercontent.com/google/fonts/main/ofl/tinos/Tinos-Regular.ttf"
get Domitian.otf         "https://mirrors.ctan.org/fonts/domitian/opentype/Domitian-Roman.otf"
get LibreBaskerville.ttf "https://raw.githubusercontent.com/impallari/Libre-Baskerville/master/fonts/ttf/LibreBaskerville-Regular.ttf"

# --- sans ---
get IBMPlexSans.ttf      "https://raw.githubusercontent.com/IBM/plex/master/packages/plex-sans/fonts/complete/ttf/IBMPlexSans-Regular.ttf"
get SourceSans3.ttf      "https://raw.githubusercontent.com/adobe-fonts/source-sans/release/TTF/SourceSans3-Regular.ttf"

# --- accessibility / dyslexia ---
get AtkinsonHLNext.ttf     "https://raw.githubusercontent.com/googlefonts/atkinson-hyperlegible-next/main/fonts/ttf/AtkinsonHyperlegibleNext-Regular.ttf"
get LexicaUltralegible.ttf "https://raw.githubusercontent.com/jacobxperez/lexica-ultralegible/main/fonts/ttf/LexicaUltralegible-Regular.ttf"
get OpenDyslexic.otf       "https://raw.githubusercontent.com/uxjulia/crossink-fonts/main/fonts/OpenDyslexic/OpenDyslexic-Regular.otf"

# Alegreya and Vollkorn are variable fonts. freetype-py cannot pin an axis, so
# they need fonttools.instancer first (build-sd-fonts.py does this) and are
# deliberately absent -- see the "not measured" note in ../FONTS.md.

echo
echo "$(ls fonts | wc -l | tr -d ' ') fonts in $(pwd)/fonts"
