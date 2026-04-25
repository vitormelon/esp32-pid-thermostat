"""
Adiciona --coverage e -lgcov às flags de link do env native_coverage.

PlatformIO trata build_flags como flags de compile. Quando o linker é
invocado para juntar o executável final com libUnity.a (que também foi
compilada com --coverage), faltam os símbolos da runtime libgcov.
Este script injeta as flags necessárias na fase de link.
"""
Import("env")

env.Append(
    LINKFLAGS=["--coverage"],
    LIBS=["gcov"],
)
