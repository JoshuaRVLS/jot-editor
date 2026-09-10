# Discord Rich Presence assets

jot's Rich Presence (ported from [iCrawl/discord-vscode](https://github.com/iCrawl/discord-vscode),
MIT -- see `LICENSE` in this directory) sends Discord *asset keys*, never images:
over IPC it can only say "show the artwork registered as `rust`". The artwork
itself has to be uploaded to the Discord application the presence authenticates
as, which is the one thing no code can do for you.

Everything needed for that is in this directory:

| Path | What it is |
| --- | --- |
| `app/jot.png` | jot branding (the app badge and the idling image). 1254x1254, the same artwork as the Windows app icon. |
| `icons/*.png` | One file per language / tool asset key, mirroring the upstream extension's asset set. |
| `languages.json` | The upstream data the asset tables were generated from (`tools/gen_discord_presence_data.py`). |
| `LICENSE` | Upstream's MIT notice, which covers `icons/`, `languages.json` and the ported tables. |

The asset **key is the file name without `.png`** -- `icons/rust.png` becomes the
key `rust`. Upload them with the same names and the defaults in `jot` work
untouched.

## Uploading the assets (one-time, ~10 minutes)

Discord requires every asset to be uploaded by hand. Start with the two that
show up immediately, then the language set when you want the pretty icons.

1. Open <https://discord.com/developers/applications> and sign in.
2. Pick the application jot uses. With the default configuration that is the
   app id in `discord_app_id` (currently `1513610110256021524`); it appears in
   `:discord status` and in `settings.conf`. If you would rather have your own,
   click **New Application**, name it `jot` (that name is what friends see
   after "Playing"), then set `discord_app_id` to the new id.
3. In the left sidebar open **Rich Presence -> Art Assets**.
4. Click **Add Image(s)** and upload:
   - `app/jot.png` -- required for the default look (it is both the small
     "app" badge and the idling image).
   - `icons/debug.png` -- used while a debug session is active.
5. Upload the language icons you care about (`icons/*.png`). All of them is
   fine: 206 files, about 5 MB total. Missing ones are harmless -- see
   *Troubleshooting* below.
6. Confirm the key names: the "Asset name" column must read exactly `jot`,
   `debug`, `rust`, `lua`, ... with no extension and unchanged case.
7. Back in jot run `:discord reconnect` (or just restart). Open a file and check
   your Discord profile.

Asset names are permanent once uploaded (the uploader lets you rename only
before saving), so uploading first and renaming later is a one-way trip.

## Which keys jot asks for

Timing does not matter much -- Discord accepts an activity referencing keys that
do not exist yet, it just renders no image (and reports the problem, which jot
surfaces through `:discord status` and the `Discord error` status chip).

Fixed keys, always referenced:

| Key | Source file | Used for |
| --- | --- | --- |
| `jot` | `app/jot.png` | small "app" badge, and the large image while idling |
| `debug` | `icons/debug.png` | small badge during a debug session |

Language keys, referenced when a file of that type is open (the same table the
status line and file explorer use for glyphs). Full list:

`ahk`, `android`, `angular`, `ansible`, `applescript`, `appveyor`, `arduino`,
`asciidoc`, `asp`, `assembly`, `astro`, `astroconfig`, `autoit`, `babel`,
`bat`, `bazel`, `bower`, `brainfuck`, `c`, `c3`, `cargo`, `circleci`,
`citrinescript`, `clojure`, `cmake`, `cobol`, `codeclimate`, `coffee`,
`contenthook`, `cosmo`, `cpp`, `crystal`, `csharp`, `csproj`, `css`, `cssmap`,
`cuda`, `cursor`, `cython`, `d`, `dart`, `debug`, `debugging`, `delphi`,
`denizen`, `docker`, `editorconfig`, `ejs`, `elixir`, `elm`, `env`, `erlang`,
`eslint`, `firebase`, `flowconfig`, `fortran`, `fountain`, `fsharp`,
`gamescript`, `gatsbyjs`, `gemfile`, `git`, `gleam`, `glsl`, `gml`, `go`,
`godot`, `gradle`, `grain`, `graphql`, `groovy`, `gruntfile`, `gulp`,
`handlebars`, `harbour`, `hare`, `haskell`, `haxe`, `heex`, `heroku`, `hjson`,
`hlsl`, `holyc`, `html`, `http`, `idle`, `idle-vscode`, `idle-vscode-
insiders`, `idle-vscodium`, `idle-vscodium-insiders`, `jar`, `java`, `jest`,
`jinja`, `jot`, `js`, `jsmap`, `json`, `jsx`, `jule`, `julia`, `jupyter`,
`kag-script`, `kirikiri-tpv-javascript`, `kivy`, `kotlin`, `laravel`, `less`,
`liquidsoap`, `lisp`, `livescript`, `log`, `lua`, `luau`, `maeel`, `makefile`,
`manifest`, `markdown`, `markdownx`, `marko`, `matlab`, `metal`, `mojo`,
`moonscript`, `nim`, `nix`, `nodemon`, `npm`, `objective-c`, `ocaml`, `odin`,
`onyx`, `opengoal`, `opengoal-goos`, `opengoal-ir`, `pascal`, `pawn`, `perl`,
`php`, `ponylang`, `postcss`, `powershell`, `prettier`, `prisma`,
`processing`, `pug`, `purescript`, `python`, `qml`, `r`, `racket`, `razor`,
`reasonml`, `restructuredtext`, `ruby`, `rust`, `scala`, `scss`, `shell`,
`skript`, `solidity`, `sourcepawn`, `sqf`, `sql`, `squirrel`, `stylus`,
`svelte`, `svg`, `swift`, `systemverilog`, `tailwind`, `templ`, `terraform`,
`tex`, `text`, `toml`, `travis`, `ts`, `tsmap`, `tsx`, `turbo`, `twig`,
`typescript-def`, `umm`, `v`, `vala`, `vb`, `vercel`, `verse`, `viteconfig`,
`vitestconfig`, `vscode`, `vscode-insiders`, `vscodeignore`, `vscodium`,
`vscodium-insiders`, `vue`, `vueconfig`, `wasm`, `webpack`, `xaml`, `xml`,
`yaml`, `yarn`, `zenscript`, `zig`, `zura`

`{lang}` / `{Lang}` / `{LANG}` in `discord_large_image` expand from the resolved
key, so the default `Editing a {LANG} file` reads `Editing a RUST file`.

## Configuration

Every key is editable from `:settings` or `settings.conf`; `:discord status`
prints the live state.

| Setting | Default | Meaning |
| --- | --- | --- |
| `discord_rpc` | `true` | Master switch (also `:discord enable` / `disable`). |
| `discord_app_id` | jot's app | Which Discord application the presence belongs to. |
| `discord_details_idling` | `Idling` | Top row with no file open. |
| `discord_details_editing` | `Editing {file_name}` | Top row while editing. |
| `discord_details_debugging` | `Debugging {file_name}` | Top row while debugging. |
| `discord_lower_details_idling` | `Idling` | Second row with no file open. |
| `discord_lower_details_editing` | `Workspace: {workspace}` | Second row while editing. |
| `discord_lower_details_debugging` | `Debugging: {workspace}` | Second row while debugging. |
| `discord_lower_details_no_workspace` | `No workspace` | `{workspace}` when no folder is open. |
| `discord_large_image` | `Editing a {LANG} file` | Hover text of the large image. |
| `discord_large_image_idling` | `Idling` | Hover text while idling. |
| `discord_small_image` | `{app_name}` | Hover text of the small badge. |
| `discord_app_image` | `jot` | Small badge asset key. |
| `discord_idle_image` | `jot` | Large image asset key while idling. |
| `discord_debug_image` | `debug` | Small badge key while debugging. |
| `discord_swap_images` | `false` | Swap the big and small images. |
| `discord_remove_details` | `false` | Hide the top row. |
| `discord_remove_lower_details` | `false` | Hide the second row. |
| `discord_remove_timestamp` | `false` | Hide the elapsed-time counter. |
| `discord_remove_repository_button` | `false` | Hide the "View Repository" button. |
| `discord_idle_timeout` | `0` | Seconds unfocused before the presence clears (`0` = never). |
| `discord_show_status` | `true` | Show the Discord chip in the status line. |
| `discord_exclude_workspaces` | empty | Regex list; matching workspace paths get no presence. |

### Placeholders

Usable in every template above; unknown placeholders are left verbatim so typos
are visible.

`{file_name}` `{dir_name}` `{full_dir_name}` `{workspace}` `{workspace_folder}`
`{workspace_and_folder}` `{current_line}` `{current_column}` `{total_lines}`
`{file_size}` `{current_errors}` `{git_branch}` `{git_repo_name}` `{lang}`
`{Lang}` `{LANG}` `{app_name}` and `{empty}` (which renders as blank, so a row
can be kept on purpose).

## Troubleshooting

**"no discord-ipc socket found"** -- Discord is not running, or its IPC socket
lives where jot does not look. jot probes every known location
(`XDG_RUNTIME_DIR`, `SNAP_USER_COMMON`, `TMPDIR`/`TMP`/`TEMP`, `/tmp`, and
indices `discord-ipc-0..9`), so a socket outside that set is the only real gap.
`:discord status` prints the endpoint that answered.

**Flatpak / snap Discord** -- sandboxed clients sometimes use a private runtime
directory that is invisible to the editor. Both flatpak and snap can be given
access, but the simplest fix (and what Discord itself recommends) is the
official download from <https://discord.com/download>.

**Images are missing but the text is fine** -- the asset key was never uploaded,
or was uploaded under a different name. Run `:discord status`: Discord reports
`Invalid Asset (...)` and jot shows it, both there and as a red `Discord error`
chip in the status line.

**Windows** -- do not run jot or Discord as administrator; the pipe becomes
invisible to a non-elevated client.

**Presence shows the wrong application name** -- the name comes from the Discord
application, not from jot. Change it in the developer portal, or point
`discord_app_id` at a different app.
