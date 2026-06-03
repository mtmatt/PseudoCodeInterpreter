# PseudoCode for Zed

This dev extension registers `.ps` files as `PseudoCode` and starts the
`pseudo-lsp` language server from the project worktree or from `PATH`.

## Local Use

From the repository root:

```sh
make lsp
```

In Zed, run `zed: install dev extension` and select this `editors/zed`
directory. Open a `.ps` file from the repository worktree so the adapter can
launch `<worktree>/pseudo-lsp`.

If you want to use the extension from another project, put `pseudo-lsp` on
`PATH` before launching Zed.

The local Tree-sitter grammar URL in `extension.toml` is absolute for this
checkout. If you move the repository, update `[grammars.pseudocode].repository`
and commit the grammar repo before reinstalling the dev extension.

If installation fails after an earlier attempt, reinstall the dev extension from
Zed. The generated checkout under `editors/zed/grammars` can be removed safely.
