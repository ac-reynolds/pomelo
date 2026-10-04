"""Stage a repository-shaped sdist; keep the checkout's packaging under python/."""
from pathlib import Path
import os
import shutil
import tempfile

from scikit_build_core.build import (
    build_wheel, build_editable, get_requires_for_build_wheel,
    get_requires_for_build_editable, get_requires_for_build_sdist,
    prepare_metadata_for_build_wheel, prepare_metadata_for_build_editable,
)
from scikit_build_core.build import build_sdist as _build_sdist


def build_sdist(sdist_directory, config_settings=None):
    destination = str(Path(sdist_directory).resolve())
    root = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory() as temporary:
        staged = Path(temporary)
        for directory in ('src', 'shaders', 'deployment', 'tests', 'inputs', 'python'):
            shutil.copytree(root / directory, staged / directory,
                            ignore=shutil.ignore_patterns('__pycache__', '*.pyc', 'build', 'dist'))
        shutil.copy2(root / 'CMakeLists.txt', staged / 'CMakeLists.txt')
        config = (root / 'python/pyproject.toml').read_text()
        config = config.replace('build-backend = "build_backend"\nbackend-path = ["."]',
                                'build-backend = "scikit_build_core.build"')
        config = config.replace('readme = "README.md"', 'readme = "python/README.md"')
        config = config.replace('cmake.source-dir = ".."', 'cmake.source-dir = "."')
        config = config.replace('wheel.packages = ["pomelo"]', 'wheel.packages = ["python/pomelo"]')
        (staged / 'pyproject.toml').write_text(config)
        previous = Path.cwd()
        try:
            os.chdir(staged)
            return _build_sdist(destination, config_settings)
        finally:
            os.chdir(previous)
