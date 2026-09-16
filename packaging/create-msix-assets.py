"""Create the image sizes required by the MSIX manifest."""

import sys
from pathlib import Path

from PIL import Image


def main() -> None:
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    icon = Image.open("assets/keyscribe-menu.png").convert("RGBA")
    for name, size in {
        "StoreLogo.png": (50, 50),
        "Logo44.png": (44, 44),
        "Logo150.png": (150, 150),
        "Logo310.png": (310, 310),
        "LogoWide.png": (310, 150),
    }.items():
        width, height = size
        canvas = Image.new("RGBA", size)
        edge = min(width, height)
        resized = icon.resize((edge, edge), Image.Resampling.LANCZOS)
        canvas.alpha_composite(resized, ((width - edge) // 2, (height - edge) // 2))
        canvas.save(output / name)


if __name__ == "__main__":
    main()
