# Photo Rating Shortcuts

- Date 2026-11-28
- Status complete on Windows
- Source audit at upstream `main` commit `99af8b992ff684d9f956abb7e4633f32cb9e4a72`
- Related work [Library batch rating](https://github.com/zidage/AlcedoStudio/pull/256),
  [Library rating operations](https://github.com/zidage/AlcedoStudio/pull/340),
  [Library cards and filmstrip ratings](https://github.com/zidage/AlcedoStudio/pull/591),
  and [Editor filmstrip batch actions](https://github.com/zidage/AlcedoStudio/pull/959)

## Product decisions

Press `1` through `5` to assign that many stars. Press `0` to clear the rating.
Use the same shortcuts in the Library and Editor. Each shortcut works throughout
its workspace. Text entry, numeric entry, dialogs, and shortcut recording take priority.

| Workspace and state | Photos that receive the rating |
| --- | --- |
| Library with a selection | Every selected photo, including selected rows outside the visible grid. |
| Library with no selection | None. A focused, hovered, or last-edited photo does not count as a selection. |
| Editor with a selection | Every selected photo. The open photo receives the rating only when it belongs to the selection. |
| Editor with no selection and an open photo | The photo shown in the Editor, including a photo outside the current Library result. |
| Editor with neither a selection nor an open photo | None. |

A key sets an absolute value. Pressing the same digit twice leaves that value in
place. Holding a digit triggers one action per physical press. The command preserves the active photo and the selection. The Library can reorder
or remove photos when an active sort, group, or filter uses the rating. Apply
the existing selection rules when the next query result arrives.

The six commands share one set of configurable bindings in **Settings > Keyboard**.
The defaults accept top-row digits and keypad keys that produce the same digits.
Keys that emit navigation events keep their navigation behavior. Existing star
controls still use their current click behavior.

In the Editor viewport, `Ctrl+1` now zooms to actual pixels. `Home` and `Ctrl+0`
reset the view. These actions use configurable registry entries and keep their
viewport focus requirement. In the Nodes graph, `Ctrl+0` still fits the graph,
and `Home` still selects Develop. Use the registry's native key labels on each
platform. Reset View has two default bindings. Assigning a custom binding
replaces both defaults, and Restore Default restores both.

When a new default conflicts with a saved custom binding in an overlapping
scope, apply the registry's current upgrade rule. Activate the default and
show the conflict on the inactive custom row in Keyboard Settings. Preserve
the saved custom setting for the user to resolve. Reject conflicting new
assignments with the existing settings message.

## Current source

The v0.3.7 Keyboard Settings list has no binding with default `1`. Its source
and the audited upstream source both handle `1` in the viewport's
`Keys.onPressed` handler. That handler also handles `0` and `Home` for reset.
The [thumbnail proposal](https://github.com/zidage/AlcedoStudio/issues/589)
refers to rating shortcuts. The audited registry has no rating commands.

Paths in the table are relative to `alcedo_studio/src/ui/alcedo_main/` unless
shown otherwise.

| Area | Current owner | Observed behavior |
| --- | --- | --- |
| Keyboard input | `shortcut_registration.cpp`, `shortcut_registry.cpp`, `qml/RegisteredShortcut.qml`, `qml/Main.qml` | The registry owns command defaults, saved bindings, and scope conflicts. The QML wrapper reads the owning window's focus and suppresses shortcuts during editable focus. Main exposes the active workspace scope. |
| Viewport navigation | `qml/EditorWorkspace.qml` | The viewport item directly handles `0`, `1`, and `Home` when it has focus and viewport input is enabled. Nodes owns separate registry commands for `Ctrl+0` and `Home`. |
| Dialogs and search | `qml/AppDialogs.qml`, `qml/SearchPalette.qml` | The dialog owner has an aggregate open check. The modal Search Palette and the image context menu are outside that check. |
| Photo targets | `album_backend/library_selection.cpp`, `album_backend/editor_session_controller.cpp`, `album_backend/workspace_router.cpp` | The Library and filmstrip share one selection. `LibrarySelection::SelectedItems()` supplies the complete selection. The Editor session exposes its open photo identity independently of the filmstrip list. |
| Rating entry points | `album_backend/library_mutation_operations.cpp`, `qml/ImageActionsController.qml`, `qml/LibraryMarksControl.qml` | The menu applies its clicked-item rule. The Library marks control rates the complete selection. `RateTargetsAsUser` checks user-control permission and dispatches single or batch writes. A Library card also checks the rating lock before calling the single-write path. |
| Rating writes | `album_backend/image_controller.cpp`, `album_backend/library_module.cpp` | A single write syncs storage before updating the view. A batch checks rating locks, shows the stars immediately, and persists on the rating worker. The batch holds a global rating lock until completion. The owner restores failed rows and publishes the result. |

This plan extends the [keyboard registry plan](configurable_keyboard_shortcut_registry_plan.md)
with a photo workflow. It uses the rating operation from the
[automation plan's AU6 phase](../automation/cli_mcp_automation_plan.md#phase-au6--selection-rating-and-delete-in-c)
and the Library presentation behavior from the
[sort and group plan](album_sort_group_and_import_time_plan.md). It depends on
the [Editor comparison plan](editor_adjustment_comparison_plan.md) for the
comparison input scope. This is a standalone design.

## Target design

### Commands and focus

| Command IDs | Settings group and label | Default | Active input scope |
| --- | --- | --- | --- |
| `photoRating.clear` | Photo Ratings / Clear rating | `0` | Library or Editor |
| `photoRating.set1` | Photo Ratings / Set rating to 1 star | `1` | Library or Editor |
| `photoRating.set2` through `photoRating.set5` | Photo Ratings / Set rating to 2 through 5 stars, one row per value | `2` through `5` | Library or Editor |
| `editor.zoomToActualPixels` | Editor / Zoom to actual pixels | `Ctrl+1` | Editor viewport focus |
| `editor.resetView` | Editor / Reset view | `Home`, `Ctrl+0` | Editor viewport focus |

Use one `workspace.photoRatings` scope for the six photo commands. It overlaps
both workspace scopes, the application scope, and coactive Editor scopes.
Resolve conflicts against this shared scope, including a mode command that
can run while rating is available. Use an `editor.viewport` local scope for
the two viewport actions. It is disjoint from the focused Nodes, filmstrip,
Versions, and LUT scopes. It overlaps any workspace or mode command that can
operate at the same time. Register these relationships in `ShortcutRegistry`
so the same rules apply in Settings and on restart.

In Main, use the workspace router and the availability of actual targets to
enable rating. Require an entered, ready project. Keep the binding available
during a rating save so the command can report why it cannot run. Use
`RegisteredShortcut` with auto-repeat disabled. Let text controls and shortcut
recording consume their own keys. Suppress photo and viewport shortcuts while
a dialog, the Search Palette, or an image menu owns input. Use dialog state
from its existing owner.

The viewport commands use the existing viewport focus item and input permission.
Remove the direct digit and Home branches when the registry takes ownership.
Use the existing viewport methods `resetView()` and `zoomToActualPixels()`.

### Target and write ownership

Add `LibraryMutationOperations::RateSelectionAsUser(int rating) -> QVariantMap`
as a QML invokable. Reuse the result fields `success`, `started`, `batch`,
`message`, and `rating` from the existing rating operation as applicable.
Include the actual target `elementId` on the single-photo result so the
caller can refresh its Inspector when that photo is shown.

Read the workspace and current selection in this operation on the UI thread.
Use the existing `SelectedItems()` target projection. When the selection is
empty in the Editor, read the valid open photo IDs from `EditorSessionController`.
Use its element ID and image ID. A nonempty selection determines the targets
even if validation reports an invalid target.

Check `UserStateChangeRefusal` through the existing user entry point. Before a
single write, check `InteractionPolicyController::EvaluateEditImageRating`
for the target. The batch path checks all targets through the existing batch
preparation. Reuse `RateTargetsAsUser` for both sizes. Keep validation in the
owner for values outside 0 through 5. The shortcut registry continues to own
settings. The existing Library owners supply the selection and persist ratings.

Present results in `ImageActionsController.qml` using the current snackbar
and Inspector refresh. Keep batch completion in its current signal handler.
A rejected command shows the owner's reason. If a batch is in progress,
report that ratings are still saving. The user can press the key again after
the save. A refusal ends the request. Each new keypress resolves the current targets.

### Success and failure paths

```text
Digit key -> RegisteredShortcut -> ImageActionsController
  -> LibraryMutationOperations::RateSelectionAsUser
  -> current selection, or the open Editor photo when selection is empty
  -> RateTargetsAsUser -> ImageController
     one photo -> SetImageRating -> storage sync -> view update -> result
     batch -> StartSetImageRatings -> immediate view update -> rating worker
       -> FinishRatingBatch -> image rating completion -> Inspector and snackbar
```

- With no target, leave the shortcut inactive. The owner also rejects a direct call with no target.
- With a policy refusal or busy save, return the operation's reason before a write starts.
  A refusal does not change ratings or add background work.
- If a write fails, present the current owner result. For a batch failure,
  the owner restores failed rows and reports failures. Preserve any package
  save warning in the result. Do not report that the project package saved
  when the owner reports a package error.
- The existing stars, Inspector, task status, and snackbar supply feedback.
  Keyboard Settings uses its existing accessible rows and capture controls.
  This feature needs no new layout, theme values, or rendering path.

## Phase R1 | Photo rating and viewport bindings

| Result | Main modules | Dependency | Expected diff | Status |
| --- | --- | --- | ---: | --- |
| Rate photos with digits and retain viewport shortcuts in their own scope | Shortcut registry, QML shell and viewport, Library rating operations | Current selection, Editor session, rating persistence | 420–750 lines | Complete on Windows |

The estimate includes source, translations, test registration, tests, and the
completion record. The viewport changes belong in the same phase as the rating
defaults because the bare viewport digits move to the rating commands.

### Implementation

1. Read root and applicable nested `AGENTS.md` files. Read
   `alcedo-qml-ui` and `alcedo-msvc-cmake` again before implementation.
2. Add stable IDs and scopes in
   `alcedo_studio/src/include/ui/alcedo_main/shortcut_registry.hpp`.
   Register the table in `shortcut_registration.cpp`. Update scope checks in
   `shortcut_registry.cpp`. Add English and Simplified Chinese entries to the
   existing translation catalogs. Update the Keyboard Settings group test.
3. Add the selection operation to the existing Library mutation header and
   implementation. Use the router, selection, and session that the operation
   already has. Add the single-photo rating-lock check to this entry point.
   Leave the context-menu target rule and the CLI route in their current owners.
4. Add a proposed `qml/PhotoRatingShortcuts.qml` component for the six
   `RegisteredShortcut` bindings. Register it in `ALCEDO_MAIN_QML_FILES`.
   Instantiate it from `Main.qml`. Read the existing target-count and
   editor-session properties for activation. Connect it to the rating
   request and result presentation in `ImageActionsController.qml`.
5. Use `AppDialogs.qml` to expose the input state for the rating and viewport
   shortcuts. Include Search Palette and image menu ownership. Keep the
   existing aggregate check for other callers.
6. Replace the direct view keys in `EditorWorkspace.qml` with two
   `RegisteredShortcut` instances. Route both commands to the existing
   viewport methods. Keep Geometry input on its current route.
7. Update the keyboard behavior descriptions for Library cards, filmstrip,
   and viewport in `alcedo_studio/src/ui/alcedo_main/DESIGN.md`. Keep the
   visual design descriptions as applicable.
8. Implement the checks below. Use current owner fixtures and focused QML
   tests. Add the photo-rating cases to the `RegisteredShortcutQmlTest` executable.

### Test and evidence requirements

| Target | Test behavior and required observation |
| --- | --- |
| `ShortcutRegistryTest` | `PhotoRatingDefaultsShareOneSettingsBindingPerValue` checks six visible bindings, both workspace conflicts, coactive Editor mode conflicts, and rebind or reset. `ViewportResetAndNodesFitMayShareCtrlZero` checks the viewport and Nodes scopes and both reset defaults. `NewPhotoDefaultsReportConflictingSavedCustomBinding` checks a saved custom collision and verifies that the saved setting remains intact. |
| `LibraryMutationOperationsTest` | `RatingUsesSharedSelectionBeforeOpenEditorPhoto` rates a selection that excludes the open photo. `EmptySelectionRatesOpenPhotoOnlyInEditor` checks the open photo outside the result, the Library with no selection, and the Editor with no targets. `ClearingRatingsWritesZeroForEverySelectedPhoto` reopens the project and checks zero on each target and the original rating on an unselected photo. `RatingRefusesLockedTargetWithoutChangingOtherRatings` checks both a single locked target and a selection containing one locked target. |
| `PhotoRatingShortcutQmlTest` cases in `RegisteredShortcutQmlTest` | `DigitShortcutsSetAbsoluteRatingsOnceInBothWorkspaces` uses the production component and checks the values 0 through 5, repeat behavior, and scope switching. `EditableFocusAndOpenDialogsKeepDigits` checks the shortcut against text and numeric entry, search, dialogs, and shortcut recording. `BusyRatingReportsReasonWithoutResubmitting` checks one refusal and no automatic submission after the busy state ends. |
| Existing rating and shortcut tests | Run `AlbumBackendRatingTest`, `RegisteredShortcutQmlTest`, `ShortcutSettingsQmlTest`, `EditorFilmstripQmlTest`, and `ImageContextMenuTargetsQmlTest`. Verify batch publication, project persistence, settings behavior, and menu targets. |
| Manual app verification | In a real window, rate a Library multi-selection and an Editor filmstrip selection that excludes the open photo. Clear the ratings with `0` and reopen the project. Check the keypad when available. Check the viewport and Nodes actions with `Ctrl+0` and `Home`, and check actual pixels with `Ctrl+1`. Rebind a rating via Settings and confirm the new key works in both views. Repeat in a real text or numeric field and with the Search Palette open. |

Cover rating filter removal and offscreen selected rows in the owner or
focused QML tests. Use the current rating fixture to check stored values, and
the current write-error checks for reporting or restoring failed ratings.
Verify that partial or package-save errors reach the existing notification
path. Use focused input tests and real-window checks as acceptance evidence.
Compare existing Keyboard Settings failures with upstream main. Upstream tracks
[the group-order failure](https://github.com/zidage/AlcedoStudio/issues/585) and
[the About navigation failure](https://github.com/zidage/AlcedoStudio/issues/978).

### Build and verification commands

Run from the repository root. Use the existing application build directory
and the platform's required dependencies. Keep logs in
`build/tmp/photo_rating_shortcuts/`.

```powershell
cmd /c scripts\msvc_env.cmd --preset win_debug
cmd /c scripts\msvc_env.cmd --build --preset win_debug --parallel 4 --target alcedo_main ShortcutRegistryTest RegisteredShortcutQmlTest ShortcutSettingsQmlTest LibraryMutationOperationsTest AlbumBackendRatingTest EditorFilmstripQmlTest ImageContextMenuTargetsQmlTest
$env:PATH = "$(Resolve-Path 'build/debug/vcpkg_installed/x64-windows/debug/bin');$env:PATH"
$ratingChecks = '^(ShortcutRegistryTest|RegisteredShortcutQmlTest|ShortcutSettingsQmlTest|LibraryMutationOperationsTest|AlbumBackendRatingTest|EditorFilmstripQmlTest|ImageContextMenuTargetsQmlTest)\.'
ctest --test-dir build/debug -N -R $ratingChecks
ctest --test-dir build/debug -j 1 --output-on-failure -R $ratingChecks
```

```sh
cmake --preset macos_debug -DALCEDO_BUILD_TESTS=ON
cmake --build --preset macos_debug --parallel 4 --target alcedo_main ShortcutRegistryTest RegisteredShortcutQmlTest ShortcutSettingsQmlTest LibraryMutationOperationsTest AlbumBackendRatingTest EditorFilmstripQmlTest ImageContextMenuTargetsQmlTest
rating_checks='^(ShortcutRegistryTest|RegisteredShortcutQmlTest|ShortcutSettingsQmlTest|LibraryMutationOperationsTest|AlbumBackendRatingTest|EditorFilmstripQmlTest|ImageContextMenuTargetsQmlTest)\.'
ctest --test-dir build/macos-debug -N -R "$rating_checks"
ctest --test-dir build/macos-debug -j 1 --output-on-failure -R "$rating_checks"
```

Record each platform separately. A test that does not receive its synthetic
key event needs real-window evidence for that path. Record that gap as
unverified until the manual check runs. Run only the named tests.

### Exit criteria

- [x] All six default rating keys reach the correct photos in both workspaces.
- [x] Rating respects input ownership and displays existing save or policy results.
- [x] The command applies to offscreen and unloaded rows in the accepted selection.
- [x] The new viewport bindings and existing Nodes bindings act only in their own focus scopes.
- [x] Stored ratings survive reopen, including a rating cleared with `0`.
- [x] The completion record identifies exercised paths, results, and unavailable checks.

### Completion record | 2026-11-28

**Status**

Complete on Windows. Implementation follows design commit `2e3eb656e` on
`feature/photo-rating-shortcuts`. macOS remains untested.

**Implementation decisions**

The three photo-rating QML cases run in the existing
`RegisteredShortcutQmlTest` executable. This reuses its Qt runtime setup.
Main also reads the Editor's locked-node mask dialog state. This dialog
belongs to the adjustment header and sits outside the application dialog owner.

**Primary success path**

```text
0–5 -> RegisteredShortcut -> ImageActionsController::requestSetSelectionRating
  -> LibraryMutationOperations::RateSelectionAsUser -> selection or open Editor photo
  -> RateTargetsAsUser -> SetImageRating or StartSetImageRatings
  -> storage and project package -> Library and filmstrip stars
```

The viewport's registered commands require viewport focus. They call
`zoomToActualPixels()` or `resetView()` on the existing interaction controller.

**Primary failure and restore path**

```text
Rating lock or active batch -> rating owner refuses -> ImageActionsController shows reason
Batch write failure -> ImageController::FinishRatingBatch restores failed view rows
  -> imageRatingsFinished -> Inspector refresh and snackbar
```

**Windows verification**

MSVC 14.44, CUDA 12.8, Qt 6.12.0, and DuckDB 1.5.6.
The phase's `win_debug` build linked `alcedo_main` and the seven named test
executables. The build used `--parallel 8` and exited 0. The corresponding
`ctest -N` command discovered 89 tests. The targeted CTest run took 139.66
seconds and exited 8.

| Suite | Passed | Failed | Skipped |
| --- | ---: | ---: | ---: |
| `ShortcutRegistryTest` | 21 | 0 | 0 |
| `RegisteredShortcutQmlTest` | 8 | 0 | 0 |
| `ShortcutSettingsQmlTest` | 16 | 1 | 0 |
| `LibraryMutationOperationsTest` | 11 | 0 | 0 |
| `AlbumBackendRatingTest` | 10 | 0 | 0 |
| `EditorFilmstripQmlTest` | 19 | 0 | 0 |
| `ImageContextMenuTargetsQmlTest` | 3 | 0 | 0 |
| Total | 88 | 1 | 0 |

All new R1 cases passed. The owner tests cleared and reopened two ratings,
rated an unloaded row in a 121-photo selection, and refused single and batch
requests while ratings were locked. The only failure was
`KeyboardCategoryIsReachableAndAboutStillOpens`. Its assertion looks up
`settingsNavItem:11`, while the unchanged Settings dialog assigns About
category 10. This matches [upstream issue #978](https://github.com/zidage/AlcedoStudio/issues/978).

The registry tests verified the viewport and Nodes binding scopes. The
packaged offscreen host stopped before QML startup because the Windows
offscreen plugin could not create the OpenGL context required by OpenCL.

The user confirmed that the manual test passed on the staged Windows
Release build of Alcedo Studio 0.3.7 on 2026-11-28.

Temporary evidence was collected under `build/tmp/photo_rating_shortcuts/`.
