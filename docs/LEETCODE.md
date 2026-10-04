# LeetCode workspace

Jot includes a bundled, jot-native LeetCode workflow inspired by the feature set of [leetcode.nvim](https://github.com/kawre/leetcode.nvim). It uses Jot's pickers, popups, commands, keymaps and language servers. It does not embed Neovim or its NUI/plenary dependencies.

## Commands

| Command | Action |
| --- | --- |
| `:Leet` / `:LeetActions` | Open the action picker |
| `:LeetList` | Browse problems by group or filter (supports `page=`, `difficulty=`, `status=`, `tags=`, `company=`) |
| `:LeetDaily` | Fetch today's problem |
| `:LeetRandom` | Fetch a random problem (supports `difficulty=`, `status=`, `tags=`) |
| `:LeetProfile` | Show solved counts for the signed-in account |
| `:LeetCookie` | Enter a session cookie in a masked prompt and store it in the OS credential store |
| `:LeetSignOut` | Remove the saved session |
| `:LeetRun` | Send the current solution and selected example to the judge |
| `:LeetTest` | Select the next example |
| `:LeetSubmit` | Submit the current solution and poll the judge |
| `:LeetReset` | Restore the selected language's platform template |
| `:LeetLang` | Choose a language and open its solution path |
| `:LeetDesc` | Show the current problem statement, topics and example count |
| `:LeetLatest` | Fetch and restore the latest submission for the active problem |
| `:LeetOpen` | Open the active problem in the configured browser or report its URL |
| `:LeetInfo` / `:LeetStatus` | Show the active question, tabs, cache TTL and solution path |
| `:LeetConsole` | Bring up the dock panel with the last run or submit result |
| `:LeetTabs` | Switch between questions opened this session |
| `:LeetYank` | Copy the active solution code to the clipboard |
| `:LeetCacheClear` | Remove cached public question data |
| `:LeetSetup name=value` | Change a supported option for the current session |

Keymap family: `Alt+Shift+L`, followed by `L` for the list, `D` for daily, `R` for random, `A` for actions, `T` for the next example, `S` to submit, `C` for the judge console, or `Y` to copy the solution. The family uses Jot's which-key display.

## Problem list and filters

`:LeetList` without filters opens five groups instead of dumping the whole problem set:

- **All problems** - the unfiltered list
- **Difficulty** - all difficulties, Easy, Medium or Hard
- **Tag** - every topic the API reports (Array, Backtracking, ...), fetched from the public tag query
- **By company** - the company list, sorted by name, then a list filter by that company
- **Favorites** - the signed-in account's LeetCode favorite lists and the problems in the chosen one. Favorite lists are per-user: the request carries the stored session when there is one and stays anonymous (and empty) when there is not, in which case the answer points at `:LeetCookie`.

Passing any filter goes straight to the list, skipping the groups.

`:LeetList` fetches one page through the paginated GraphQL question list. LeetCode caps a single page at 100 questions, so the picker's last row, **Load more problems**, fetches the next page and reopens the picker with everything loaded so far. The row's detail shows how many questions are loaded out of the total. `page=n` starts directly on a page instead (the picker then still offers load-more from there). The same pagination applies to a favorite list's problems.

Filters are given as `name=value` pairs:

- `difficulty=easy|medium|hard` - passed through uppercased
- `status=solved|todo` - mapped to the API's `UserQuestionStatus` enum (`AC` / `NOT_STARTED`). LeetCode has no server-side "attempted" filter, so that value is reported as unsupported and dropped instead of failing the request.
- `tags=array,hash-table` - tag slugs, comma or space separated
- `company=amazon` - a company slug; company problem lists may require a premium session and an empty result reports that
- `page=n` - starting page

```vim
:LeetList difficulty=easy, tags=array
:LeetList company=amazon
:LeetRandom difficulty=hard, status=todo
```

Rows show the frontend id, title, difficulty and, when the account has progress, solved/attempted/todo. The list doubles as a checklist: a solved problem leads with a green check, an attempted one with an amber dot, and a fresh one carries no mark so what is left to do stands out. Public list requests never carry the stored session cookie; tag and company-picker queries are public too.

## Task feedback

Every network request reports itself, because a silent request looks like a dead command:

- the status line carries a live `leet: ...` segment while anything is in flight (an operation label, or the number of pending requests)
- request start and completion are announced as messages (`-> POST /graphql/`, `<- POST /graphql/ HTTP 200`, blocked/stopped/not queued cases)
- list loads announce `Loading problem list (page N)...` and `Loaded N of M problem(s)`

## Dock panel

Opening a problem brings up the LeetCode panel in the right dock (the secondary sidebar), next to the code instead of over it:

- the question: frontend id, title, difficulty (green easy, amber medium, red hard) and the selected language plus example count
- **Completed** with a green check when the account has already solved the problem, or **Attempted** with an amber dot when it was only tried
- the statement prose, wrapped to the dock, and the selected example rendered like the site: **Input**, **Output** and **Explanation** labelled values inside bordered blocks
- **Run test** and **Submit** as clickable rows with an icon, and **Next example** to cycle the example
- the judge console below them: an in-flight line while the request runs, then the outcome with an icon and a colour per result - a green check for Accepted and passing case counts, a red cross for a failing status or case count, red compile and runtime error lines

A row is highlighted on hover and runs on click through the panel's own callback, so no command has to be typed. `:LeetRun`, `:LeetSubmit` and `:LeetTest` (and the `Alt+Shift+L` keymaps) still work and drive the same panel; `:LeetConsole` brings the panel up. The panel's rows scroll with the wheel when the console is longer than the dock.

## Configuration

```lua
jot.leetcode_feature.setup({
  endpoint = "leetcode.com",
  language = "cpp",
  solution_dir = "/path/to/solutions",
  cache_ttl = 604800,
  timeout = 20,
  list_limit = 100,
  api_base = "",
})
```

The China host can be selected with `endpoint = "leetcode.cn"`. `api_base` is intended for local fixtures and API-compatible endpoints. Session-bearing requests are restricted to the official LeetCode HTTPS domains; a custom API base never receives a saved session cookie.

Problem metadata is cached under the OS cache directory (`XDG_CACHE_HOME` on POSIX, `LOCALAPPDATA` on Windows). Cookies are not written to config, cache, request body files, shell commands or logs.

## Sign in and credential storage

There is no embedded browser or supported LeetCode OAuth callback in Jot, so opening LeetCode in a browser does not sign Jot in automatically. Run `:LeetCookie`, open LeetCode in your browser, copy the value of the `Cookie` request header (not the `Cookie:` label), paste it into Jot's masked prompt, then press Enter. You can also paste only the `LEETCODE_SESSION` value. The prompt is a themed panel that strips pasted line breaks, echoes the stored value as stars, warns when Enter is pressed with nothing to save, and checks the session as soon as it is stored. Escape cancels without saving.

`:LeetOpen` only opens the active problem page in the system browser. It cannot read that browser's cookies or transfer its login session to Jot. The feature stores the cookie under a Jot-specific key in the platform credential service:

- Windows Credential Manager
- Apple Keychain
- Linux Secret Service through `secret-tool` with a live D-Bus session

When the operating-system store is unavailable, sign-in reports that state and does not fall back to plaintext storage. The cookie may be either the complete cookie header string or the `LEETCODE_SESSION` value. If a `csrftoken` cookie is present in the stored header, Jot sends the matching CSRF header on authenticated requests.

## Current scope and limits

The real-binary PTY probe covers the five problem groups, the difficulty, tag and company pickers filtering the list, the signed-out favorite-list answer, the list loading indicator and completion feedback, pagination through the load-more row, credential-free public requests, the masked sign-in prompt and stored session, the refusal to send a session to a non-official domain, selecting a problem from the list, the solution template being written to disk and opened, and the dock panel: its question, the Run test / Submit rows, the hover highlight on an actionable row, a click on Run test starting the judge, and the console reporting the outcome. The probe's API fixture is not an official LeetCode domain, so its judge request is refused before it leaves the process by design; the success-report rendering (green check, passing case counts, status, output, errors) is pinned by the Lua unit tests instead.

The full online account/judge workflow requires a stored session and the official domain, and has not been exercised end-to-end from this repository's CI. On Linux, the Secret Service prompt is skipped when there is no D-Bus session. Endpoint schemas are undocumented by LeetCode and may change. Reference features not implemented here: inject, fold/toggle, stats, menu and exit.

MIT License. This feature is a separate implementation using the public API shape as reference. No leetcode.nvim source is bundled.
