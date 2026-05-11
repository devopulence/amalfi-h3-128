"""Wheel build hook — platform-specific, Python-version-agnostic.

The package bundles a native shared library (libh3_extended.{dylib,so})
so the wheel must be platform-tagged (not 'any'). But it has no Python
C extension — cffi ABI mode loads the lib at runtime via dlopen — so
the python+abi tags should be py3+none, NOT cpXY+cpXY.

Without this hook, setuptools auto-tags the wheel with the build host's
CPython ABI (e.g. cp311-cp311), and pip rejects it on any other Python
minor version. The Databricks serverless runtime is Python 3.12, so a
cp311 wheel built by our CI cannot install there.

Target tag: py3-none-<platform> — works on Python 3.9+ with cffi.
"""

from setuptools import setup
from setuptools.dist import Distribution

try:
    from wheel.bdist_wheel import bdist_wheel as _bdist_wheel
except ImportError:  # newer setuptools relocated the command
    from setuptools.command.bdist_wheel import bdist_wheel as _bdist_wheel


class bdist_wheel(_bdist_wheel):  # noqa: N801 — matches setuptools naming
    """Force py3-none-<platform> tag for the bundled-lib wheel."""

    def finalize_options(self):
        super().finalize_options()
        self.root_is_pure = False  # platform-specific (has the .so/.dylib)

    def get_tag(self):
        _, _, plat = super().get_tag()
        return "py3", "none", plat


class BinaryDistribution(Distribution):
    """No Python C extensions, but platform-specific data (.so / .dylib).

    Combined with the bdist_wheel override above, yields py3-none-<plat>.
    """

    def has_ext_modules(self):  # type: ignore[override]
        return False

    def is_pure(self):  # type: ignore[override]
        return False


setup(distclass=BinaryDistribution, cmdclass={"bdist_wheel": bdist_wheel})
