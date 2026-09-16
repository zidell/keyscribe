"""Render the static download page for one release."""

import re
import sys
from pathlib import Path


def main() -> None:
    version, output_directory = sys.argv[1:3]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("Version must be MAJOR.MINOR.PATCH")
    output = Path(output_directory)
    output.mkdir(parents=True, exist_ok=True)
    template = Path(__file__).resolve().parents[1] / "site" / "index.html"
    html = template.read_text(encoding="utf-8")
    html = html.replace("__VERSION__", version)
    (output / "index.html").write_text(html, encoding="utf-8")
    (output / "_worker.js").write_bytes((template.parent / "_worker.js").read_bytes())
    (output / "logo.svg").write_bytes((template.parents[1] / "assets" / "keyscribe.svg").read_bytes())
    (output / "version.txt").write_text(version + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
