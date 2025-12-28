# Regression corpus

Each file here documents a specific bug that was fixed. Naming convention:

    bugNNN_short-description.xs

The file must pass on the default backend. If it ever starts failing
again, the regression has come back. A short comment at the top of each
file links to the issue or commit that first fixed it.

Tests under this directory are picked up automatically by
`tests/run-all.sh`.
