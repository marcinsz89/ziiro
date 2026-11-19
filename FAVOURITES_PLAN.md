# Favourites feature plan

Planned 2026-11-17 against the current working tree. **Plan only; not implemented.**

## Intended behavior

Mark an amiibo as a favourite, then open **Favourites** as a fourth browser grouping alongside Amiibo Series, Game Franchises and Platforms. It opens directly to one flat, alphabetical list of amiibos: no series, franchise, console, or game subgroups, thumbnails, or preview panel. Opening a row uses the existing figure details and NFC flow.

Navigation decisions for implementation:

- **G Group** cycles Series → Franchise → Platform → Favourites → Series. The new grouping is the requested separate tab/group; **Tab** keeps its existing Library/Catalog role.
- **V** toggles the selected figure's favourite mark in a figure list, confirmed search results, or figure details. **F** already means Up. Show `V Favourite` / `V Unfavourite` in the relevant footer and a small mark in ordinary lists/details. Group headings cannot be favourited.
- Favourites respects Library/Catalog: Library includes marked models with a local backup; Catalog includes all marked models available in the loaded catalog/local scan. Losing a backup hides the row only in Library and never deletes the mark.
- Display only names in the Favourites list. Distinct model IDs with the same name remain separate rows; the figure detail identifies the edition. Sort case-insensitively by display name, then original name, then model ID for deterministic ties. Retain punctuation; avoid adding a locale dependency for this scoped change.
- Empty state: `No favourites in this view` with a brief hint to open a figure and press V. Empty-list Enter/Up/Down must be harmless. Removing the last visible favourite keeps this empty Favourites view.
- Search from Favourites searches only favourites within the current Library/Catalog scope. While typing, V remains a query character. Esc returns to the prior list and selection. Enter opens a result; Esc from details returns to that result/list.
- During NFC checking/start/presentation/stop, retain the current input restrictions. Do not rebuild lists while an active presentation holds a selected figure.

The interpretation of “raw list” here is text-only browser rows; the existing figure screen remains available after Enter. Other grouping layouts and the user's miniature setting stay as they are when leaving Favourites.

## State and persistence

Use a set of normalized **16-digit model IDs**, independent of backup paths, catalog order, names, or ownership. Existing `Figure.id` and `Catalog::scan()` already merge copies of the same model. A favourite applies to all copies; renaming/moving a backup or downloading new metadata preserves it. Locally identified models without catalog metadata can also be marked.

Store one lowercase ID per line in `<profile>/favourites.txt`: `~/ziiro/favourites.txt` on the current device and the preview's selected `--profile` directory on desktop. Keep this separate from `preferences.tsv`, whose current writer rewrites only its two known settings. A missing file means no favourites; ignore malformed lines and duplicate IDs without discarding valid entries. Report an unreadable existing file and disable writes for that session until it can be loaded, avoiding accidental replacement of unknown saved state.

Save on every toggle using a unique temporary file in the same profile directory, checked writes/close, and atomic rename. Publish the new in-memory set only after saving succeeds. On failure, retain the prior mark and list and display `Could not save favourites`. Store sorted IDs for repeatable output. Do not silently prune IDs absent from the current catalog or local scan; they may return after a later rescan/download. App upgrades must leave this user file untouched.

## Implementation scope

| File | Planned change |
| --- | --- |
| `studio.h` | Add `STUDIO_FAVOURITE` to the shared input actions. |
| `preview.c`, `device.c` | Map V to the new action only outside search text entry; prevent held-key repeat from repeatedly toggling. |
| `studio.cpp` | Own/load/save the ID set; extend `Grouping`; derive a flat favourite pointer list; integrate browser, search, figure selection, empty state, footer and indicators. |
| `README.md`, `DEVICE.md`, `HANDOFF.md` | Document the shortcut, grouping, persistence and observed desktop/device validation after implementation. |

Keep the persistence helpers small and separate from rendering within `studio.cpp`; no new catalog field or backend process is needed. `catalog.cpp` and `catalog.hpp` should need no changes: favourites are user state, not metadata or backup content. Only extract a persistence module if the implementation warrants it.

Order of work:

1. Add ID loading and atomic persistence, then the shared action and desktop/device V mappings.
2. Add `Grouping::Favourites` with direct entry to the flat list. Extend `figure_list()`/`figure_selection()` and navigation so the new view does not index `ui.groups[ui.group]` when no group exists. Render full-width text rows for this grouping without changing `ui.show_miniatures`.
3. Add mark/unmark to lists, confirmed search and details. Preserve selection by ID after rebuilding. When the current row is removed, select the next row, otherwise the previous row. From details, return safely to the updated list when unmarking a favourite would remove the active row.
4. Scope `filter_search()` to favourites when appropriate. Rebuild pointer lists only after `refresh()` replaces `ui.catalog`; clear all old figure/search/game pointers before redraw. Keep `return_to`, `search_origin`, cursor and scroll offsets valid after rescan, catalog sync, view changes and removals.
5. Update guides with what actually passed; package/deploy only as a separate requested implementation step.

## Verification and completion criteria

The state owner is the per-user profile; the catalog is read-only input to this feature. The affected behavior is browser navigation, selection and persistence. Failure is visible in the UI and must leave the previous file/mark usable. Input and disk saves stay synchronous on the existing UI path; no background writer or concurrent profile editing is introduced.

- Build the SDL preview and ARM64 target using [DEVICE.md](DEVICE.md). Use a temporary profile for desktop exercises and keep the personal collection unchanged.
- Mark figures from normal lists, confirmed search and details; verify one mark per model despite duplicate backups. Check mixed-case names, duplicate names, ordering, a single favourite, removal of first/middle/last rows and empty-state keys.
- Restart the preview; rescan; rename/move/remove/restore a backup; refresh catalog names/order; switch Library/Catalog and every grouping; exercise search/back/detail navigation. Marks must survive and no stale selection may open a different model.
- Exercise missing/malformed/duplicate-ID files, an unreadable favourites file and failed temporary write/rename. Verify a failed toggle retains the prior state and reports failure. Use a small focused persistence test only if the helpers can be tested meaningfully without mirroring implementation.
- Verify text-only Favourites at 320×170 and that ordinary miniature settings/layouts return unchanged. Check V does not toggle during text entry, on key repeat, or while NFC is active.
- On a physical Zero, verify V and G, restart persistence, LCD readability, and returning from a favourite to normal NFC presentation/stop. Record SKU, image/kernel and actual outcomes. Desktop checks do not establish hardware behavior.

Completion means favourites persist, the new grouping is a flat alphabetical text list, existing browsing/search/NFC remain usable, and failed saves cannot silently erase marks. No extra favourite folders, custom ordering, cloud sync, artwork downloads, or Store publishing are part of this feature.
