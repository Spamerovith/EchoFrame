"""Normalize the Geode packager ZIP for reliable Windows loader startup."""

import os
import sys
import tempfile
import zipfile
from pathlib import Path


def main() -> None:
    archive = Path(sys.argv[1])
    fd, temporary = tempfile.mkstemp(suffix=".geode", dir=archive.parent)
    os.close(fd)
    try:
        with zipfile.ZipFile(archive) as source, zipfile.ZipFile(
            temporary, "w", zipfile.ZIP_DEFLATED
        ) as target:
            names = source.namelist()
            first = [
                "mod.json",
                "echoframe.echoframe.dll",
                ".geode_cache",
                "logo.png",
                "about.md",
            ]
            for name in first + [name for name in names if name not in first]:
                target.writestr(name, source.read(name))
        with zipfile.ZipFile(temporary) as check:
            if check.testzip() is not None:
                raise RuntimeError("Invalid repacked Geode archive")
        os.replace(temporary, archive)
    finally:
        if os.path.exists(temporary):
            os.remove(temporary)


if __name__ == "__main__":
    main()
