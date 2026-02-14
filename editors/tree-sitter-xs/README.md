# tree-sitter-xs

Tree-sitter grammar for XS. Covers the core language: bindings, functions,
pattern matching, classes/traits/enums, effects, concurrency primitives,
and the universal literal forms (durations, colors, dates, sizes, angles).

## Building

```sh
npm install
npx tree-sitter generate
npx tree-sitter test
```

To run the parser against a source file:

```sh
npx tree-sitter parse path/to/file.xs
```

## Using

### Neovim (`nvim-treesitter`)

```lua
require'nvim-treesitter.configs'.setup {
  ensure_installed = { 'xs' },
  highlight = { enable = true },
}

-- and register the parser:
local parser_config = require "nvim-treesitter.parsers".get_parser_configs()
parser_config.xs = {
  install_info = {
    url = "https://github.com/xs-lang0/xs",
    files = { "editors/tree-sitter-xs/src/parser.c" },
    location = "editors/tree-sitter-xs",
  },
  filetype = "xs",
}
```

### Helix

Drop an entry into `languages.toml`:

```toml
[[language]]
name = "xs"
scope = "source.xs"
file-types = ["xs"]
roots = ["xs.toml"]
comment-token = "--"
auto-format = false

[[grammar]]
name = "xs"
source = { git = "https://github.com/xs-lang0/xs", subpath = "editors/tree-sitter-xs" }
```

### Zed

`extension.toml`:

```toml
[grammars.xs]
repository = "https://github.com/xs-lang0/xs"
commit = "HEAD"
path = "editors/tree-sitter-xs"
```

## Scope

The grammar recognizes most common XS source, including:
- gradual type annotations and `where` clauses
- `fn`, `fn*` generators, lambdas, and default / variadic parameters
- `struct`, `enum`, `trait`, `impl`, `class`, `type`, `effect`
- pattern matching with guards, destructuring, range patterns
- async, spawn, actors, channels, nurseries
- algebraic effects: `perform`, `handle`, `resume`
- pipe operator, null coalescing, spread
- universal literals: `500ms`, `#ff6600`, `2025-01-20`, `10MB`, `45deg`

Plugin-injected syntax (parser-override plugins, lexer transforms) is
inherently beyond a static grammar, so files that rely on those will
highlight correctly only in the ranges where standard XS is used.
