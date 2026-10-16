# External dependencies

Dependency checkouts are created locally and are absent from source archives.
`dependencies.lock.json` pins their repositories and commits;
`patches/manifest.json` owns the checksummed project changes.

From the repository root, use Python 3.10+ and Git:

```sh
python3 scripts/bootstrap_dependencies.py
python3 scripts/apply_dependency_patches.py
python3 scripts/apply_dependency_patches.py --check
```

Use `python` on Windows. Bootstrap `--only NAME` is repeatable; selecting a
nested dependency also prepares its parent instead of creating an unrelated
checkout inside it. The [developer guide](../docs/DEVELOPMENT.md) lists the
smaller dependency set for ROM-free policy tests.

Patch checks validate the ordered stack without changing checkout files or
the real index. Normal application resumes pristine or exact reviewed
intermediate states and refuses unexpected local edits. Preserve those edits
before retrying; see [dependency recovery](../docs/TROUBLESHOOTING.md#source-dependency-preparation-fails).

Do not edit or distribute dependency checkouts. Express changes as reviewed
patches. Dependency preparation alone does not generate game payloads or
produce a complete runtime; follow [Building](../docs/BUILDING.md).
