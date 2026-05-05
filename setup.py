"""Wheel build hook — forces a platform-specific wheel.

The package bundles a native shared library (libh3_extended.{dylib,so}),
so the wheel must NOT be tagged as 'pure' (py3-none-any). Setuptools
auto-detects the platform tag when an Extension is present, but here
the binary is precompiled by cmake and only included as package_data,
so we override `is_pure` and `has_ext_modules` to mark it impure.
"""

from setuptools import setup
from setuptools.dist import Distribution


class BinaryDistribution(Distribution):
    def has_ext_modules(self):  # type: ignore[override]
        return True

    def is_pure(self):  # type: ignore[override]
        return False


setup(distclass=BinaryDistribution)
