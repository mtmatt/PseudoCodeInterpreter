use zed_extension_api::{self as zed, LanguageServerId, Result, Worktree};

struct PseudoCodeExtension;

impl zed::Extension for PseudoCodeExtension {
    fn new() -> Self {
        Self
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &LanguageServerId,
        worktree: &Worktree,
    ) -> Result<zed::Command> {
        let command = worktree
            .which("pseudo-lsp")
            .unwrap_or_else(|| format!("{}/pseudo-lsp", worktree.root_path()));

        Ok(zed::Command {
            command,
            args: Vec::new(),
            env: Vec::new(),
        })
    }
}

zed::register_extension!(PseudoCodeExtension);
