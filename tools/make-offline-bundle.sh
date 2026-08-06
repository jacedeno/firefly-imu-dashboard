#!/usr/bin/env bash
# Build a fully self-contained copy of the dashboard for offline demos.
#
# The hosted dashboard pulls Three.js from unpkg.com and fonts from Google Fonts,
# so it degrades badly on venue Wi-Fi. This vendors every external asset and
# rewrites index.html to local paths, leaving a folder that needs no internet.
#
# Run it while you still have a good connection. See DEMO.md.
#
#   ./tools/make-offline-bundle.sh ~/blueghost-demo
#   cd ~/blueghost-demo && python3 -m http.server 8765
#   google-chrome --enable-experimental-web-platform-features http://localhost:8765/
#
# http://localhost is a secure context, so Web Bluetooth works over plain HTTP.

set -euo pipefail

THREE_VERSION="0.128.0"
DEST="${1:-}"

if [[ -z "$DEST" ]]; then
  echo "usage: $0 <destination-dir>" >&2
  exit 2
fi

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$REPO/docs"

if [[ ! -f "$SRC/index.html" ]]; then
  echo "error: $SRC/index.html not found — run this from inside the repo" >&2
  exit 1
fi

if [[ -e "$DEST" ]]; then
  echo "error: $DEST already exists — remove it or pick another path" >&2
  exit 1
fi

echo "==> copying dashboard to $DEST"
mkdir -p "$DEST"
cp -r "$SRC/." "$DEST/"
mkdir -p "$DEST/vendor/three" "$DEST/vendor/fonts"

echo "==> vendoring three.js $THREE_VERSION"
BASE="https://unpkg.com/three@${THREE_VERSION}"
THREE_FILES=(
  "build/three.min.js"
  "examples/js/controls/OrbitControls.js"
  "examples/js/shaders/CopyShader.js"
  "examples/js/shaders/LuminosityHighPassShader.js"
  "examples/js/postprocessing/EffectComposer.js"
  "examples/js/postprocessing/RenderPass.js"
  "examples/js/postprocessing/ShaderPass.js"
  "examples/js/postprocessing/MaskPass.js"
  "examples/js/postprocessing/UnrealBloomPass.js"
)
for f in "${THREE_FILES[@]}"; do
  out="$DEST/vendor/three/$(basename "$f")"
  curl -fsSL "$BASE/$f" -o "$out"
  printf '    %s (%s)\n' "$(basename "$f")" "$(du -h "$out" | cut -f1)"
done

echo "==> vendoring webfonts"
# A desktop UA is required, otherwise Google serves legacy ttf instead of woff2.
UA="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/151.0.0.0 Safari/537.36"
CSS_URL="https://fonts.googleapis.com/css2?family=Rajdhani:wght@500;600;700&family=JetBrains+Mono:wght@400;500;700&display=swap"
curl -fsSL -A "$UA" "$CSS_URL" -o "$DEST/vendor/fonts/fonts.css"

font_count=0
while read -r url; do
  [[ -z "$url" ]] && continue
  fn="$(basename "$url")"
  curl -fsSL "$url" -o "$DEST/vendor/fonts/$fn"
  # rewrite the absolute gstatic URL to the local filename
  sed -i "s|${url}|${fn}|g" "$DEST/vendor/fonts/fonts.css"
  font_count=$((font_count + 1))
done < <(grep -oE "https://fonts\.gstatic\.com[^)]+" "$DEST/vendor/fonts/fonts.css" | sort -u)
echo "    $font_count font files"

echo "==> rewriting index.html to local paths"
python3 - "$DEST/index.html" <<'PY'
import re, sys

path = sys.argv[1]
html = open(path, encoding="utf-8").read()

# three.js CDN -> vendor/three/<basename>
html = re.sub(
    r"https://unpkg\.com/three@[0-9.]+/"
    r"(?:build|examples/js/(?:controls|shaders|postprocessing))/([A-Za-z0-9_.]+\.js)",
    r"vendor/three/\1",
    html,
)

# Google Fonts stylesheet -> local css
html = re.sub(
    r'<link[^>]*href="https://fonts\.googleapis\.com/css2[^"]*"[^>]*>',
    '<link rel="stylesheet" href="vendor/fonts/fonts.css">',
    html,
)

# preconnect hints are dead weight offline and cost a DNS timeout each
html = re.sub(r"\s*<link rel=\"preconnect\"[^>]*>", "", html)

open(path, "w", encoding="utf-8").write(html)

remaining = sorted(set(re.findall(r"https?://[^\"')\s]+", html)))
if remaining:
    print("    remaining external references (non-blocking):")
    for r in remaining:
        print("      ", r)
PY

echo "==> writing start-demo.sh launcher"
cat > "$DEST/start-demo.sh" <<'LAUNCHER'
#!/usr/bin/env bash
# Blue Ghost IMU - offline demo launcher.
# Serves this folder on localhost and opens Chrome with Web Bluetooth enabled.
# No internet needed: http://localhost is a secure context, so Web Bluetooth
# works over plain HTTP and every asset is vendored inside this folder.
#
#   ./start-demo.sh              # port 8765
#   PORT=9000 ./start-demo.sh    # if that port is taken
set -euo pipefail
PORT="${PORT:-8765}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The flag is only read at startup: if Chrome is already running this would
# silently open a tab in the existing process and Web Bluetooth would be absent,
# which looks exactly like the Connect button doing nothing.
if pgrep -x chrome >/dev/null 2>&1; then
  echo "Chrome is already running - the flag would be IGNORED."
  echo "Quit Chrome completely (pgrep -c chrome must be 0), then run this again."
  exit 1
fi

echo "Serving $DIR on http://localhost:$PORT"
python3 -m http.server "$PORT" --directory "$DIR" >/dev/null 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true' EXIT
sleep 1

google-chrome --enable-experimental-web-platform-features "http://localhost:$PORT/"

echo "Chrome closed; stopping the server."
LAUNCHER
chmod +x "$DEST/start-demo.sh"

echo
echo "Offline bundle ready: $DEST"
echo
echo "  cd $DEST && ./start-demo.sh"
echo
echo "  (or by hand: python3 -m http.server 8765, quit Chrome, then"
echo "   google-chrome --enable-experimental-web-platform-features http://localhost:8765/)"
