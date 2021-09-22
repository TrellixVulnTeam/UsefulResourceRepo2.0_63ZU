# JetBrains IDE Keymap for Visual Studio Code.

[![Marketplace](https://vsmarketplacebadge.apphb.com/version-short/isudox.vscode-jetbrains-keybindings.svg)](https://marketplace.visualstudio.com/items?itemName=isudox.vscode-jetbrains-keybindings)
[![Installs](https://vsmarketplacebadge.apphb.com/installs/isudox.vscode-jetbrains-keybindings.svg)](https://marketplace.visualstudio.com/items?itemName=isudox.vscode-jetbrains-keybindings)
[![Rating](https://vsmarketplacebadge.apphb.com/rating-short/isudox.vscode-jetbrains-keybindings.svg)](https://marketplace.visualstudio.com/items?itemName=isudox.vscode-jetbrains-keybindings)
[![GitHub issues](https://img.shields.io/github/issues/isudox/vscode-jetbrains-keybindings.svg)](https://github.com/isudox/vscode-jetbrains-keybindings/issues)
[![GitHub pull requests](https://img.shields.io/github/issues-pr/isudox/vscode-jetbrains-keybindings.svg)](https://github.com/isudox/vscode-jetbrains-keybindings/pulls)
[![GitHub license](https://img.shields.io/github/license/isudox/vscode-jetbrains-keybindings.svg)](https://github.com/isudox/vscode-jetbrains-keybindings/blob/master/LICENSE)

Inspired by Microsoft vscode-sublime-keybindings.

This extension ports the most popular IDE of JetBrains keymap to VS Code. After installing the extension and restarting VS Code your keymap from JetBrains are now available.

This keymap has covered most of keyboard shortcuts of VS Code, and makes VS Code more 'JetBrains IDE like'.

But this extension hasn't transfer all keybindings of JetBrains yet. If you want more feature, go to [GitHub Issues](https://github.com/isudox/vscode-jetbrains-keybindings/issues) and make it more effective for your developement.

All of the customed keymap in this extension is configured in `./package.json` file. You can add or modify keymap configurations as seen below.

```json
{
  "mac": "<keyboard shortcut for mac>",
  "linux": "<keyboard shortcut for linux>",
  "win": "<keyboard shortcut for windows>",
  "key": "<default keyboard shortcut>",
  "command": "<name of the command in VS Code>"
}
```

> **Tip:** If you want to use <kbd>ctrl</kbd> (or <kbd>cmd</kbd> in macOS) + click to jump to definition change `editor.multiCursorModifier` to `alt` and restart VS Code.

## What keybindings are included in this extension?

You can see all the keyboard shortcuts in the extension's contribution list.
![extension contributions](https://raw.githubusercontent.com/Microsoft/vscode-sublime-keybindings/master/contributions_list.png)

You can read more about how to contribute keybindings in extensions in the [official documentation](http://code.visualstudio.com/docs/extensionAPI/extension-points#_contributeskeybindings).

## License

[MIT](LICENSE)
