# PseudoCode for Zed

This dev extension registers `.ps` files as `PseudoCode` and starts the
`pseudo-lsp` language server from the project worktree or from `PATH`.

## Local Use

From the repository root:

```sh
make
```

In Zed, run `zed: install dev extension` and select this `editors/zed`
directory. Open a `.ps` file from the repository worktree so the adapter can
launch `<worktree>/pseudo-lsp`.

If you want to use the extension from another project, put `pseudo-lsp` on
`PATH` before launching Zed:

```sh
make lsp
sudo cp pseudo-lsp /usr/local/bin/pseudo-lsp
```

If `/usr/local/bin` is not on your shell path, copy it to another directory that
is on `PATH`, or add the repository root to `PATH` before starting Zed:

```sh
export PATH="/path/to/PseudoCodeInterpreter:$PATH"
zed .
```

Use `zed: open log` if highlighting or diagnostics do not load. Grammar
compilation errors and language-server launch failures are reported there.

The Tree-sitter grammar is stored as the `tree-sitter-pseudocode` submodule and
is pinned by commit in `extension.toml`. If you update the grammar, push the
grammar repository first, then update `[grammars.pseudocode].commit`.

If installation fails after an earlier attempt, reinstall the dev extension from
Zed. The generated checkout under `editors/zed/grammars` can be removed safely.
