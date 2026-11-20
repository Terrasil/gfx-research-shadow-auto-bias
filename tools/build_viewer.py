"""Bundle the GLSL, JavaScript, and offline fixtures into one portable HTML file."""
import argparse
import base64
import gzip
import json
from pathlib import Path


def visibility_source(project_root):
    shader = (project_root / "shaders" / "shadow.frag").read_text(encoding="utf-8")
    header = ("#version 300 es\nprecision highp float;\nprecision highp int;\n"
              "precision highp sampler2D;\nprecision highp usampler2D;")
    return shader.replace("#version 460 core", header, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, default=Path("viewer/cases"))
    parser.add_argument("--out", type=Path, default=Path("shadow-auto-bias.html"))
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1] / "viewer"
    data = []
    for case in sorted(args.fixtures.glob("*/fixture.json")):
        record = json.loads(case.read_text(encoding="utf-8"))
        record["files"] = {
            blob.stem: base64.b64encode(gzip.compress(blob.read_bytes(), compresslevel=6, mtime=0)).decode("ascii")
            for blob in sorted(case.parent.glob("*.bin"))
        }
        data.append(record)
    if not data:
        parser.error("no case fixtures were found")
    payload = '\n'.join([
        '<script id="visibility-shader" type="text/plain">' +
        visibility_source(source.parent).strip() + '</script>',
        '<script id="fixtures" type="application/json">' + json.dumps(data, separators=(",", ":")) + '</script>',
        '<script>' + (source / "lab.js").read_text(encoding="utf-8") + '</script>',
    ])
    html = (source / "index.html").read_text(encoding="utf-8").replace("<!-- PAYLOAD -->", payload)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(html, encoding="utf-8")
    print(f"Bundled {len(data)} fixtures into {args.out}")


if __name__ == "__main__":
    main()
