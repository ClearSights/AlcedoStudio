# Project Maintenance Feature Plan

Date: 2026-11-09

Status: planned. Product implementation has not started.

Source audit: `7bcac45d`.

Parent: [Import and Project Maintenance coordination plan](duplicate_image_cleanup_and_import_plan.md).

Related feature: [Import duplicate skipping](import_duplicate_skipping_plan.md).
Project Maintenance can ship independently of that feature.

## 1. Approved product decisions

Add Project Maintenance as a separate application feature.
Its workflow is **Scan -> Review -> Remove selected**.
Users review existing project entries before any removal.

- Add a **Project Maintenance** text button immediately below **Background Tasks** in the top-left
  Collections sidebar. Use one shared dialog and controller. A new SVG is not required.
- Scan the entire loaded project, regardless of the selected album, filters, or search.
- Identify repeated imports by normalized full source path. Preserve every folder component.
  Equal names or equal bytes under different full paths do not make maintenance duplicates.
- Keep every rated or edited entry. This includes inactive Versions, undone history, and additional
  or renamed Versions. A missing source does not remove this protection.
- Identify missing sources separately from permission errors, unreadable paths, and broken project
  references. Do not convert an access failure into a missing-file finding.
- Show full source paths, entry IDs, ratings, editing evidence, and reasons in the review.
  Users select eligible entries and can clear that selection before removal.
- Back up the complete current project before deletion. Verify that backup before proceeding.
- Remove project entries only. Never delete original source images.
- Publish a successful result only after checked removal, package writing, and project reopening.
  Report real failures. Do not continue through a substitute deletion or recovery algorithm.

The user has approved these decisions and requested planning only at this stage.
Do not start implementation while updating this document.

The earlier proposal for CLI-only cleanup is superseded. CLI support now uses the same application
service as the UI. The earlier Photos operation is completed operational work, not this feature.

## 2. Product specification

### 2.1 Entry point and dialog

The Collections utility rows appear in this order:

```text
Search
Background Tasks
Project Maintenance
```

The button opens the following shared dialog:

```text
Project Maintenance                                      Close
Current project name

Scan project
Scan progress and current phase, when running

Findings: All / Duplicates / Missing sources / Other problems
Search full source path or entry ID

Select  Full source path  Entry ID  Rating  Editing work  Finding  Action/reason
[ ]     ...              ...       ...     ...           ...      ...

Select removable duplicates   Select removable missing entries   Clear selection
Selected: N entries

Backup location and status, when a removal is prepared or completed
Cancel / Remove selected
```

Use a text utility row with the same alignment and spacing as the existing utility rows.
Preserve the other rows and the collection tree. Add no icon asset, status dot, or decorative badge.
Expose the same dialog from the editor rail below its Background Tasks action so the entry point
remains available when switching workspaces. Both launchers use the same controller and findings.

Use `AppTheme` tokens and existing shared controls. Use `ThemeCheckBox` for row selection and
`DialogActionButton` for actions. Use `AlertBadge` for actual errors. Keep neutral results as plain
text. Give selection a neutral outline or surface treatment from the existing design rules.
Add English and Simplified Chinese strings. Register new QML files in the application CMake list.
Update the applicable `docs/VI/README.md` entries during UI implementation.

### 2.2 States and selection

| State | Required behavior |
| --- | --- |
| No loaded project | Disable the launcher and explain that a project must be opened |
| Project loading or retiring | Disable Scan and Remove selected; show the existing owner reason |
| Ready, no scan | Show Scan project; no implicit scan or deletion |
| Scanning | Show progress and Cancel scan; removal is disabled |
| Scan canceled | Show Canceled; partial findings are informational and cannot be applied |
| No findings | Show No duplicate or invalid entries found; removal stays disabled |
| Review | Show completed findings; only eligible entries have enabled selection controls |
| Preparing removal | Settle conflicting writers; finalize editor work; save and back up |
| Applying removal | Disable selection changes and cancellation; retain the operation task |
| Complete | Show actual removed and retained counts, the backup path, and Refresh scan |
| Failed | Show the failed stage and recovery status; never show a successful removal count |

Start every completed scan with no entries selected. The two explicit selection actions select only
currently eligible findings. Users can clear individual rows or all rows. The service revalidates
selection before mutation. The UI cannot override a rated, edited, or uncertain entry's protection.

For a readable duplicate group, retain every protected entry. When no entry is protected, retain
the earliest import by `Element.added_time`, then the lowest element ID. Show the retained entry
beside removable extras. That retained entry cannot be selected as a removal target. A user can
leave additional extras by clearing their selection. Multiple edited copies stay separate.

A missing, unrated entry with complete proof of no editing work is removable without a survivor.
A missing source with rating or editing work is shown as protected. Permission failures, an
unavailable volume, non-file paths, and ambiguous broken references remain unselected and blocked.
Show their exact error and affected full path. The user can fix the source or project and rescan.

Close hides the dialog during a scan; the task continues in Background Tasks. Reopening returns to
the same task. Cancel scan stops further reads and releases its resources. Closing the dialog does
not approve removal. Once apply begins, closing the dialog hides it but does not interrupt the
backup/publication operation. Project close waits through the existing task shutdown handling.

Tab reaches the launcher, filters, path search, enabled checkboxes, selection actions, and footer
actions in native order. Space toggles focused enabled checkboxes. Enter invokes the focused
button. Escape hides the dialog during scan/review and cancels an unstarted confirmation. It does
not terminate an applying operation. Announce stage and count changes through accessible text.
All paths wrap in an expanded row; a narrow window changes rows to stacked field/value layouts.
Virtualized rows remain bounded. Search and view filters never remove hidden selected IDs.
The confirmation always states the complete selected count, including hidden rows.

### 2.3 Removal confirmation and recovery

Remove selected opens a confirmation containing the actual selected counts by finding kind.
State that original image files remain untouched. State that rated and edited entries are protected.
Confirming starts backup and validation; Cancel changes nothing.

Choose a unique side-by-side backup name such as
`Photos.backup-before-maintenance-2026-11-09-001.alcd`.
Never overwrite an earlier backup. Show its full path when verified.
If the package directory cannot hold a backup, fail explicitly before deletion and let the user
choose another backup directory. Do not silently move the backup into an unrelated cache.

The feature does not add library deletion to image edit Undo/Redo. Existing Undo/Redo and Versions
remain unchanged for retained entries. Recovery of removed entries uses the verified backup through
the normal project-open workflow. A successful removal does not remove that backup automatically.

## 3. Findings, identity, and preservation rules

### 3.1 Identity and first-release scan scope

Reuse the proposed neutral `NormalizeSourceImagePath(const image_path_t&)` operation in
`src/include/utils/file/source_image_path.hpp` and `src/utils/file/source_image_path.cpp`.
It normalizes absolute path spelling, directory separators, and dot segments. On Windows it uses
case-insensitive comparison. On macOS this feature preserves case and performs no filesystem case
probe. Never merge distinct visible paths by resolving symlinks,
hard links, filenames, capture times, EXIF, or content hashes.

This utility can be introduced by either feature's first phase. The other feature reuses it.
Project Maintenance does not depend on the import fingerprint table, the import preference, or the
import admission changes. It works when those changes have not shipped.

The first release checks these concrete findings:

| Finding | Evidence | Removal eligibility |
| --- | --- | --- |
| Repeated full source path | Several live project entries have the same normalized path | Unrated pristine extras only; retain designated survivors |
| Missing source | A source status/read reports file-not-found after checking the containing volume | Unrated pristine entries only |
| Source access error | Permission, volume, sharing, or other read failure | Report only; no automatic missing classification |
| Source is not a regular file | Path names a directory or another unsupported filesystem object | Report only |
| Broken project reference | Missing Image, Element, root, state, Version, pipeline, or referenced asset | Report and protect; ownership is uncertain |
| Invalid stored history | Invalid JSON, inconsistent ownership/labels, dangling commit parents, or invalid graph | Report and protect |

Do not fully decode every RAW file to perform this scan. Do not claim that a successful filesystem
check proves that every source pixel can decode. Decoder diagnostics remain with their existing
import/render owners. Maintenance distinguishes a verified missing source from an unavailable drive
before offering removal; it does not preselect every path on a disconnected volume.

### 3.2 Proof that an entry has no editing work

Check through the existing data owners. The library's edited indicator is insufficient because it
reads only the active Version's head. A removable entry must satisfy every condition below:

1. Image and element bindings exist and belong to the requested entry.
2. Both `Image.rating` and the metadata `Rating` are zero and agree.
3. `CommitGraphStore::LoadGraph` succeeds. Its root has zero stored commits, including unreachable
   or undone commits. Any stored commit protects the entry.
4. The entry has exactly its original Default Version at the empty root head. Additional or
   renamed Versions protect the entry. A stored root head is `''`, not necessarily SQL NULL.
5. Root and checkpoint decoding succeeds through the existing pipeline-document APIs.
   Root element ID, active Version, materialized head, and chain labels agree.
6. Checkpoint and `PipelineParam` documents equal this entry's immutable import root document.
   A difference is editing evidence. Compare each entry with its own root, not another entry's
   root ID. Root identity includes the element ID.
7. No active editor lease, provisional edit, pending journal, or unresolved save can affect it.
8. Removal cannot discard a manual description, rating reason, or other owner-managed annotation.
   Treat uncertain annotation ownership as protected.

An intentional library duplicate can share its Image ID with another element. Keep these bindings
protected during automatic candidate selection. The storage helper must still respect reference
counts if another valid caller removes an unshared entry. Missing or inconsistent evidence always
produces a protected finding; the UI never converts an inspection failure into deletion permission.

### 3.3 Minimal results and lifetime

Add a derived finding representation containing entry ID, source path for display, finding kind,
preservation reasons, rating, commit/Version counts, and an optional retained entry ID.
These are bounded read results. Do not include copied Image, CommitGraph, pipeline documents,
image buffers, or asset blobs. Read those through owners while validating.

`ProjectMaintenanceService` owns scan results until refresh, project change, or service destruction.
The QML model reads those results and owns row selection. Applying produces only selected IDs and
remove/retain ID pairs. It does not write an old report over current state. The report is advisory;
the apply owner rereads every selected entry before mutation.

## 4. Current implementation audit

Paths in this table are relative to `alcedo_studio/`.

| Area | Current owner and source | Observed behavior | Planned change |
| --- | --- | --- | --- |
| Sidebar | `src/ui/alcedo_main/qml/CollectionsPanel.qml:20` | Search and Background Tasks utility rows | Add text action below Background Tasks |
| Dialog routing | `src/ui/alcedo_main/qml/Main.qml:680`, `src/ui/alcedo_main/qml/AppDialogs.qml` | Main forwards utility signals to shared dialogs | Add one maintenance launcher/dialog |
| Editor rail | `src/ui/alcedo_main/qml/EditorWorkspaceRail.qml:302` | Background Tasks action exists | Add matching text entry to the same shared dialog |
| Background tasks | `src/include/ui/alcedo_main/album_backend/background_task_controller.hpp` | UI-thread RegisterTask/UpdateTask/FinishTask, cancellation, and locks | Add scan/apply kinds; reuse lifecycle |
| Interaction policy | `src/include/ui/alcedo_main/album_backend/interaction_policy_controller.hpp` | Reads existing task locks and explains disabled controls | Extend admission to maintenance conflicts across UI and automation |
| Mutation adapter | `src/include/ui/alcedo_main/album_backend/library_mutation_operations.hpp` | DeleteScope follows the selected folder | Add explicit whole-project maintenance operation |
| Delete owner | `src/ui/alcedo_main/album_backend/image_controller.cpp:873` | Deletes library bindings, history, image data, then saves; several errors are swallowed | Use checked owner helpers for one staged storage transaction |
| Storage | `src/storage/store/sleeve/element_store.cpp` | Owns memberships and element/file persistence | Add focused batch validation/removal with caller transaction |
| History | `src/storage/store/edit_history/commit_graph_store.cpp:555` | DeleteGraphForElement starts its own transaction | Expose transaction-neutral deletion helper for the batch owner |
| Edited SQL | `src/include/storage/store/sleeve/album_edit_state_sql.hpp:29` | Active Version head only | Keep this presentation rule separate from maintenance preservation proof |
| Masks | `src/include/storage/store/mask_asset/mask_asset_store.hpp` | Verifies content-addressed mask rows; caller owns connection | Preserve every asset referenced by retained history |
| Package API | `src/include/app/project_package_service.hpp` | Exports a live database and reads/writes checked packages | Add verified backup and checked publication operations |
| Package publication | `src/app/project_package_backend.cpp:1187` | Writes a temporary package, removes the target, then renames | Add an atomic replacement operation with explicit restore result |
| Project lifecycle | `src/include/ui/alcedo_main/album_backend/project_handler.hpp` | Save/package jobs and close/load owner boundaries | Settle without history GC; activate or restore through these owners |

`PersistProjectForClose` performs unreachable-history cleanup. Do not invoke that cleanup as an
incidental maintenance preparation step. This feature promises to preserve complete retained
history rows. Materialize/save current work through a checked operation that does not run history
garbage collection. After staging succeeds, close the old runtime without saving it again before
opening the published result. Otherwise an ordinary close-save could overwrite the cleaned package.

Relevant registered targets are `SleeveServiceTest`, `PipelineMapperTest`,
`ProjectPackageBackendTest`, `AlbumBackendImageDeleteTest`, `LibraryMutationOperationsTest`,
`AlbumBackendBackgroundTaskTest`, and `AlbumBackendInteractionPolicyTest`.
New owner/controller tests are specified below. Confirm discovery before selecting their cases.

Related plans:

- [Background Tasks and declarative UI state](../ui/background_tasks_ui_state_plan.md): reuse current
  registry/policy owners; confirm source rather than relying on its historical type lists.
- [Album sorting and import time](../ui/album_sort_group_and_import_time_plan.md): preserve survivor
  import time and use full project scope independently of album queries.
- [Library search and project size](library_search_and_project_size_plan.md): preserve the compact
  Image mapping and current storage boundaries.
- [CLI and shared-session automation](../automation/cli_mcp_automation_plan.md): use one application
  operation from UI and commands; expose explicit selection rather than implicit delete-all.

## 5. Target ownership and failure boundaries

### 5.1 Service, controller, and scan

`ApplicationModuleHost` owns both the new `ProjectMaintenanceService` application facade and the
new `ProjectMaintenanceController`. Both survive replacement of the current project. The service
depends on application/storage types, not UI types. ProjectService supplies scoped owner access;
it does not own the maintenance service's lifetime. The service exposes scoped reads, scan
cancellation, and staged apply operations. It owns no independent live Image/history model and
acquires Database connections at operation boundaries.

The controller publishes task state and a paged `ProjectMaintenanceModel` on the UI thread.
Service project bindings and job references exist only while inspecting or staging the current
project. Release those references and close staged connections before activation. The controller
orchestrates ProjectHandler/ProjectLaunchCoordinator activation, then binds the service to the
newly installed owners. Only the controller publishes whole-operation success after activation.

Use bounded database pages for scan enumeration. Release database locks before slow filesystem
reads. Check active editor ownership on its owning thread. Active entries are protected during
scan; apply later finalizes and rechecks them. Scan completion is not a global immutable database
capture. The report records derived findings from scoped reads, and apply performs fresh checks.

Scan success chain:

```text
Collections/Editor rail -> AppDialogs -> ProjectMaintenanceController
  -> BackgroundTaskController registration -> ProjectMaintenanceService worker
  -> scoped storage/history reads and source status -> completed findings
  -> UI-thread model publication -> review selection
```

Scan failure chain:

```text
entry read/status failure -> protected finding with exact reason
fatal owner failure or cancel -> incomplete task -> no apply eligibility -> release scan resources
```

### 5.2 Apply, backup, staged storage, and activation

The apply operation owns one complete maintenance lifecycle:

1. Validate a nonempty selection of completed findings. Refuse a second apply.
2. Reserve conflicting project mutations through the existing task/admission owners. Stop new
   UI and automation writes. Settle already admitted rating, import, delete, export, editor-save,
   semantic/annotation, and package-save jobs before capture. Do not cancel user work silently.
3. Finalize the editor and materialize its journal through its owner. Check every result.
   Save the complete baseline without running history garbage collection.
4. Create a unique complete project backup and verify package checksum, project identity, and a
   content digest. A backup failure stops before any removal.
5. Export/unpack a working package artifact through `ProjectPackageService`. Use Database and its
   storage owners against that staged artifact. Do not start a second live ProjectService or copy
   in-memory Image/history objects. The staged database exists specifically so a failed removal
   or package write cannot partly mutate the active project.
6. Revalidate every selected ID and preservation rule in the staged database. Recheck source
   statuses and retained targets. Validate all pairs before mutation.
7. Run one checked storage transaction for all selected removals and membership changes.
8. Write a new checked package from the staged database. Independently reopen it and check retained
   IDs, ratings, complete histories, masks, reference integrity, and summaries.
9. Under the existing package owner write exclusion, ensure that the original still represents
   the captured baseline. A changed baseline stops publication and leaves the current original
   untouched. Atomically replace the original package with the verified result. Record whether
   this operation actually installed that result and its verified content digest.
10. Close the old runtime without an additional save. Reopen through ProjectHandler and
    ProjectLaunchCoordinator. Release old references before installing the new project owners.
11. Publish actual counts and the backup path only after activation succeeds. Release mutation
    reservation and task locks only after success or completed restore.

The backup and staged package are necessary independent artifacts. An owner operation on the live
database alone cannot undo a later package-write or activation failure. Capture the saved metadata,
database, and required embedded assets only. The verified backup is immutable recovery data and
remains until the user removes it. The working artifact is independently editable only by the
maintenance storage owner, for this operation's lifetime. Discard it after success or completed
restore. Neither artifact is copied back as an old mutable Image/history representation.

Database or validation failure rolls back the whole staged transaction. Backup, staged write,
prepublication verification failure, changed-baseline refusal, or failed atomic replacement leaves
the current original untouched. These outcomes must not restore an older backup over that file.

Restore is permitted only after this operation is known to have installed its verified cleaned
package. Before restoring for an activation failure, the package owner verifies that the current
original still has this operation's published digest. If another writer has changed it, leave that
externally changed file untouched and report recovery required. Keep the backup and staged evidence.
An unknown publication outcome also requires explicit recovery rather than an assumed restore.

When the current original is still the package installed by this operation, the package owner's
explicit restore operation can restore the verified backup, then the controller reopens that
baseline. A restore/reopen failure reports both errors, keeps conflicting writes disabled, and
exposes the backup path. It must not publish success or open another project as a substitute.
Retain required recovery artifacts and report the real failure. Release task/admission locks after
success or completed restore; keep conflicting writes disabled while recovery remains unresolved.

### 5.3 Storage operation and batch invariants

Add a focused operation accepting only removal IDs and optional retained IDs. It owns one
connection, the Database lock, and one transaction in the staged database.

- Removal IDs are unique and nonzero. No retained ID appears anywhere in the complete removal set.
- Every duplicate pair belongs to one normalized full-path group. Missing-source removals have
  no retained target. Reject self-pairs, chains, cycles, missing targets, and protected entries.
- Validate the complete batch before changing any row. A bad selection removes nothing.
- Union removed duplicate entries' album memberships into the nominated survivor before deletion.
  The `(folder_id, element_id)` key prevents duplicate memberships. Recompute affected reference
  counts through the owning mapper. Missing-entry removal has no membership transfer.
- Remove dependent Element, FileImage, PipelineParam, VersionRef, ImageEditState, and PipelineRoot
  rows through transaction-neutral owner helpers. Handle EditCommitAttribution with its commits.
- Keep every retained root, Version, commit, checkpoint, rating, annotation, and mask reference.
  Delete an Image only after its last FileImage reference is gone. Keep shared mask assets unless
  their owner proves that no retained pipeline or history refers to them.
- Remove semantic/AI rows for removed entries through their owners. If the import feature's
  optional fingerprint table exists, the last-Image-reference removal owner also removes its unused
  fingerprint. That integration belongs to the import feature's storage owner; maintenance calls
  the ordinary Image removal operation and acquires no import feature dependency. Do not add
  independent SQL copies of those owners.
- Publish no notifications until the transaction and package activation are complete.

### 5.4 Executable concurrency cases

Do not add generation counters, report epochs, or a second cancellation protocol.
Use existing cancellation for user-requested scan cancellation and existing operation admission.
Cover these real current-path orderings:

1. Scan reads pristine entry E. After the worker queues the finding to the UI, the editor or rating
   owner changes E. Apply must finalize current work and reject E on fresh owner validation.
2. A rating worker is already admitted when Apply begins. If Apply captures before that worker
   commits and the worker writes afterward, the backup or deletion would omit current work.
   Apply must wait for that admitted writer and refuse new writes through both UI and automation.
3. An asynchronous package-save job is already running through MakeSaveAndPackageJob. Maintenance
   must settle that job and share package-owner write exclusion before capture/publication.
4. Another saved package replaces the captured source before publication. Test a real package writer
   at that boundary. Detect changed baseline before replacement and fail explicitly, keeping the
   backup and staged result. Never overwrite a newly observed package with an older staged one.

Drive tests through these owner/executor boundaries, not by injecting impossible revision values.
Project close cancels/waits for scan and waits for applying tasks through the existing shutdown
owners, so a queued result cannot publish into a destroyed project controller.

## 6. File and API map

All source paths below are relative to `alcedo_studio/`. Proposed files do not yet exist.

| Area | Current files that change | Proposed files and responsibility |
| --- | --- | --- |
| Full path identity | Domain CMake files | `src/include/utils/file/source_image_path.hpp`, `src/utils/file/source_image_path.cpp`; shared comparison utility |
| Application service | `src/include/app/project_service.hpp`, `src/app/project_service.cpp` for focused owner access if required; app CMake | `src/include/app/project_maintenance_service.hpp`, `src/app/project_maintenance_service.cpp`; host-owned facade with scoped project bindings |
| Library storage | `src/include/storage/store/sleeve/element_store.hpp`, `src/storage/store/sleeve/element_store.cpp`, associated mappers | Focused removal operation and transaction-neutral helpers |
| History/assets | CommitGraphStore, MaskAssetStore, annotation and image stores | Reuse existing owners; add caller-transaction helpers where required |
| Package/lifecycle | `src/app/project_package_service.cpp`, `src/app/project_package_backend.cpp`, defining headers; ProjectHandler/ProjectLaunchCoordinator | Verified backup, staged publication, checked restore, and activation |
| UI owner | `src/ui/alcedo_main/album_backend/application_module_host.cpp` and header; task/policy controllers | `src/include/ui/alcedo_main/album_backend/project_maintenance_controller.hpp` and implementation; host owns service/controller and coordinates activation |
| Review model | UI CMake | `project_maintenance_model.hpp/.cpp`; derived finding rows and selection |
| QML | CollectionsPanel, EditorWorkspaceRail, Main, AppDialogs, application CMake | `ProjectMaintenanceDialog.qml`; shared review workflow |
| UI documentation | `src/ui/alcedo_main/DESIGN.md`, `docs/VI/README.md`, translations | Existing tokens and per-file decisions; no new palette |
| Automation | `automation/commands/project_commands.cpp`, task tracker and command tests | `project.maintenance.scan`, paged results, and explicit remove-selected command |

Proposed APIs:

| API | Input and result | Owner and mutation behavior |
| --- | --- | --- |
| `ScanProjectMaintenance` | Current project owner, cancellation/progress callbacks -> completed findings or explicit error | Application service; read-only project inspection |
| `VisitMaintenanceFindings` | Offset/count/filter -> const derived result page | Service/model; bounded result lifetime until refresh/project change |
| `InspectMaintenancePreservation` | Element ID -> protection reasons and minimal counts | Existing live/history owners; no mutable internals exposed |
| `RemoveSelectedProjectEntries` | Span of removal IDs and optional retained IDs -> staged operation outcome | Application service; validates, backs up, and prepares/publishes through package owner; controller completes activation and final task result |
| `RemovePristineProjectEntries` | Minimal validated change descriptions -> row counts | ElementStore; same transaction as related owner helpers |
| `CreateVerifiedProjectBackup` | Saved package path and selected backup directory -> backup path/digest | Package owner; never overwrites an existing backup |
| `PublishVerifiedMaintenancePackage` | Checked staged package and baseline/backup identities -> explicit publication outcome and installed digest | Package owner; no UI types or activation. Controller publishes final success after lifecycle activation |

Use defining headers rather than new forward declarations. Value types contain only new derived
results or the minimal change descriptions. Include span/optional and defining owner headers at
their actual use sites. Add no long-lived DuckDB connections to new storage controllers.

## 7. Phase summary

| Phase | Result | Main modules | Dependency | Expected changed lines | Status |
| --- | --- | --- | --- | ---: | --- |
| PM1 | Full-path scan and preservation classification | Shared utility, app service, storage/history reads | None | 900-1,500 | planned |
| PM2 | Sidebar launcher, background scan, paged review and selection | Controller/model/QML/task policy | PM1 | 1,100-1,800 | planned |
| PM3 | Checked whole-project storage removal transaction | Element/Image/history/asset stores | PM1 | 1,100-1,900 | planned |
| PM4 | Verified backup, staged publication, activation and restore | Package/service/project lifecycle | PM2, PM3 | 1,000-1,800 | planned |
| PM5 | UI/automation parity and product qualification | Commands, caller tests, translations, docs | PM4 | 700-1,300 | planned |

Each estimate includes production source, tests, build registration, resources, and documentation.
PM3 is a storage capability tested on isolated fixtures. The product Remove selected action remains
disabled until PM4 supplies the complete indivisible backup/publication lifecycle. Split a phase
before implementation if its expected diff can exceed 2,000 lines. The features share only neutral
path comparison; import detection is not a prerequisite for any maintenance phase.

Before implementation, reread AGENTS.md, CONTRIBUTING.md, `alcedo-create-plan`, `alcedo-qml-ui`, and
`alcedo-msvc-cmake`. Confirm current source and the parent plan. Do not implement during this update.

## 8. Detailed phases

### PM1 - Full-path scan and preservation classification

**Objective and deliverables:** Add a read-only service that returns complete, bounded findings.
Prove the distinction between repeated full paths, missing sources, and protected work.

**Inputs and prerequisites:** Current source; shared path utility if already delivered by import.
Otherwise introduce that utility here with the same API. No fingerprint database is required.

**Modules, files, and APIs:** Proposed ProjectMaintenanceService and result definitions; shared path
utility; ElementStore scoped reads; CommitGraphStore reads; source-status utility; app CMake.

**Data rules and invariants:** Apply sections 3 and 5. Read complete graph preservation evidence.
Keep filesystem I/O outside database locks. Store derived findings only. Scan writes no project row.

**Implementation steps:**

1. Add/reuse NormalizeSourceImagePath with platform-aware full-path tests.
2. Enumerate live project bindings in bounded pages through storage owners.
3. Classify source status without treating permission or volume errors as file-not-found.
4. Inspect every preservation rule through live and persisted owners.
5. Choose survivors and removable extras deterministically. Validate all result IDs.
6. Return completed findings with exact protected/error reasons and bounded paging.

**Primary success call chain:** service -> scoped owners -> full-path groups/status -> findings.

**Primary failure and restore call chain:** invalid per-entry evidence -> protected finding;
fatal owner failure/cancel -> incomplete scan with no apply result. Existing state is unchanged.

**Tests and evidence:** Add proposed `SourceImagePathTest` and `ProjectMaintenanceServiceTest`.
`DifferentFoldersWithEqualNamesRemainSeparate` asserts separate groups, including equal-byte files.
`EquivalentWindowsSlashSpellingsShareOnePath` asserts one group without dropping directories.
`RatedUneditedEntryIsProtected`, `InactiveVersionAndUndoneCommitsProtectEntry`, and
`RenamedVersionProtectsEntry` assert disabled removal eligibility.
`MissingSourceWithEditingWorkRemainsProtected` verifies intersecting reasons.
`PermissionAndUnavailableVolumeAreNotMissingSources` asserts report-only results.
`BrokenReferenceProtectsItsEntry` and `PipelineDifferenceProtectsEntry` assert no removable ID.
`ScanLeavesAllProjectRowsUnchanged` compares ordered cell digests before/after.

**Build and run commands:** Use section 10; build SourceImagePathTest and
ProjectMaintenanceServiceTest. Discover and run their cases only.

**Exit criteria:** [ ] Every finding has an exact reason. [ ] All preservation/path/error tests pass.
[ ] Scan creates no database, history, or source-file mutation. [ ] Result paging stays bounded.

**Expected diff:** 900-1,500 lines, including tests and source registration.

**Completion record:** Not started. Fill section 11 during execution.

### PM2 - Background scan, launchers, review, and selection

**Objective and deliverables:** Expose the approved UI and selection flow. Keep removal unavailable
until PM4 is complete. Users can scan, inspect, cancel, hide, reopen, and select eligible findings.

**Inputs and prerequisites:** PM1 completed findings and owner errors; current QML design rules.

**Modules, files, and APIs:** Proposed ProjectMaintenanceController/Model/Dialog; ApplicationModuleHost;
CollectionsPanel; EditorWorkspaceRail; Main/AppDialogs; BackgroundTaskController; interaction policy.

**Data rules and invariants:** The host owns the controller across project reloads. The service owns
findings; the model owns selection. Rows cannot enable protected entries. Partial results cannot apply.

**Implementation steps:**

1. Add task kinds and route scan progress/cancel callbacks to the existing registry on the UI thread.
2. Add the two text launchers and one shared dialog route without a new SVG.
3. Implement the paged model's display roles, filters, selected IDs, and protected reasons.
4. Implement the explicit selection actions and total selected count across filters/pages.
5. Implement empty/error/canceled states and hide/reopen behavior.
6. Add keyboard/accessibility names, wrapping, theme tokens, translations, QML registration, and VI records.

**Primary success call chain:** launcher -> shared controller -> service/task -> model -> review.

**Primary failure and restore call chain:** owner failure/cancel -> task terminal state and disabled
apply; project change clears results/selection after pending scan callbacks have settled.

**Tests and evidence:** Add proposed `ProjectMaintenanceControllerTest` and `ProjectMaintenanceModelTest`.
`MaintenanceLauncherFollowsBackgroundTasks` asserts action order and shared dialog routing.
`ReviewCannotSelectRatedOrEditedRows` asserts disabled selection through model APIs.
`HiddenSelectedRowsRemainInConfirmationCount` asserts selection survives filtering/paging.
`ScanCancelLeavesApplyDisabled` and `DialogReopenShowsCurrentScan` assert task continuity.
`ProjectChangeReleasesOldResults` checks owner lifetime and no late publication.
Manually check both themes, keyboard navigation, long paths, and narrow-window row reflow.

**Build and run commands:** Build the proposed controller/model targets plus
AlbumBackendBackgroundTaskTest and AlbumBackendInteractionPolicyTest. Use targeted discovered cases.

**Exit criteria:** [ ] All approved review states work. [ ] Protected selection is rejected in C++.
[ ] Background task cancellation/close behavior passes. [ ] Manual VI/accessibility record exists.
[ ] Remove selected remains unavailable before PM4.

**Expected diff:** 1,100-1,800 lines.

**Completion record:** Not started. Fill section 11 during execution.

### PM3 - Whole-project checked storage removal

**Objective and deliverables:** Add a focused atomic storage operation for eligible IDs, independently
of the currently selected album. Exercise it only through isolated storage fixtures at this phase.

**Inputs and prerequisites:** PM1 preservation rules and minimal change descriptions.
The staged package lifecycle does not exist yet, so the UI cannot call this mutator directly.

**Modules, files, and APIs:** ElementStore, related mappers, ImageStore, CommitGraphStore, annotation
owners, MaskAssetStore, optional fingerprint owner; proposed RemovePristineProjectEntries.

**Data rules and invariants:** One operation-boundary connection/lock and one transaction.
Validate every pair before mutation. Preserve all protected rows and survivor-owned data.

**Implementation steps:**

1. Validate unique removals, same-path pairs, retained targets, complete preservation evidence, and source status.
2. Refactor existing transaction-owning deletion helpers to provide caller-transaction operations.
3. Union duplicate album memberships and recompute affected reference counts.
4. Delete only the reviewed bindings and dependent owner rows.
5. Preserve shared Images/assets and every retained root, Version, commit, checkpoint, and annotation.
6. Commit once and return exact removed IDs/counts; propagate the first real owner error after rollback.

**Primary success call chain:** staged service -> ElementStore transaction -> owner helpers -> exact IDs.

**Primary failure and restore call chain:** invalid batch -> zero mutations; injected owner failure
after partial statements -> transaction rollback -> unchanged isolated database.

**Tests and evidence:** Add proposed `ProjectMaintenanceStorageTest`.
`SelectedAlbumDoesNotLimitProjectRemoval` checks whole-project removal and all memberships.
`DuplicateRemovalTransfersMembershipsWithoutDuplicates` asserts membership union and ref counts.
`RatedOrEditedSelectionRejectsWholeBatch` asserts no other selected row is deleted.
`RemovalChainsCyclesAndRepeatedIdsChangeNothing` covers A->B->C and A->B->A.
`LastImageReferenceControlsImageRemoval` verifies shared ownership.
`RetainedHistoryAndMasksMatchBeforeRemoval` compares complete serialized rows and asset bytes.
`InjectedOwnerFailureRollsBackAllRelatedTables` injects failure after memberships/history change.
`MissingEntryRemovalRequiresPristineProof` checks no survivor is needed but all protection remains.

**Build and run commands:** Build ProjectMaintenanceStorageTest, SleeveServiceTest, PipelineMapperTest,
and AlbumBackendImageDeleteTest. Run targeted affected cases and direct callers using section 10.

**Exit criteria:** [ ] Batch validation is complete before mutation. [ ] All rollback/ref-count tests
pass. [ ] No history or image state is copied back over a live owner. [ ] No UI mutator is enabled.

**Expected diff:** 1,100-1,900 lines.

**Completion record:** Not started. Fill section 11 during execution.

### PM4 - Verified backup, staged publication, activation, and restore

**Objective and deliverables:** Enable one complete user removal operation with checked backup,
staged database mutation, atomic package publication, and owner replacement or verified restore.

**Inputs and prerequisites:** PM2 review/controller and PM3 storage operation. Required live owner
admission and checked save APIs must be added within this phase before enabling Remove selected.

**Modules, files, and APIs:** ProjectMaintenanceService/Controller; ProjectPackageService/backend;
ProjectHandler/ProjectLaunchCoordinator; existing task/admission and shutdown owners.

**Data rules and invariants:** Apply section 5.2 in order. Preserve complete history without cleanup
GC. Independent artifacts have the documented purpose/lifetime. No second live owner model exists.

**Implementation steps:**

1. Extend shared owner admission for both UI and automation; settle existing conflicting jobs.
2. Finalize and save current work without history GC; verify a unique immutable backup.
3. Create the staged artifact through package APIs and use its Database/storage owners for PM3.
4. Revalidate selected rows/statuses after capture; fail the entire batch if protection changed.
5. Write and independently reopen the result; compare retained rows and reference integrity.
6. Add checked atomic publication instead of remove-target-then-rename behavior.
7. Close the old runtime without resaving it; activate through the existing project lifecycle.
8. Restore/reopen only if this operation installed the current verified package. Leave a changed
   baseline or externally changed published file untouched; hold recovery when ownership is uncertain.
9. Publish actual counts and release locks only after the complete outcome is known.

**Primary success call chain:** confirmation -> settle/save -> verified backup -> staged transaction
-> verified package -> atomic replacement -> old-owner release -> reopen -> result/unlock.

**Primary failure and restore call chain:** backup/staging/prepublication failure -> current original
unchanged and no restore; verified installation followed by activation failure -> confirm current
installed digest -> restore -> baseline reopen -> error/unlock. An externally changed current file
or unknown installation result -> leave current file untouched -> recovery required. Restore failure
-> explicit recovery error, retained artifacts, disabled writes, and backup path. Never report
backup-only success.

**Tests and evidence:** Add proposed `ProjectMaintenanceApplyTest` and extend ProjectPackageBackendTest.
`BackupFailurePerformsNoRemoval` asserts all package and database bytes remain.
`ExistingBackupIsNeverOverwritten` checks unique names and verified contents.
`AlreadyAdmittedRatingFinishesBeforeCapture` drives the actual worker/admission ordering.
`NewAutomationWriteIsRejectedDuringApply` checks shared admission rather than only QML state.
`ReviewThenEditPreventsSelectedRemoval` checks revalidation after a real settled edit.
`StagedWriteFailureLeavesOriginalPackageIntact` and `AtomicReplaceFailureRetainsOriginal` verify failure boundaries.
`ChangedBaselineNeverTriggersBackupRestore` asserts the external writer's bytes remain unchanged.
`ExternallyChangedPublishedPackageIsNotOverwrittenDuringRecovery` changes the installed package
before injected activation failure and asserts no backup restore touches the changed file.
`ActivationFailureRestoresVerifiedBackup` checks project identity and preserved rows after reopening.
`RestoreFailureReportsBothErrorsAndKeepsWritesDisabled` asserts no false success.
`OldRuntimeCannotResaveOverPublishedMaintenance` covers the close/open save ordering.
`ConcurrentPackageSaveIsSettledBeforeMaintenance` and `ChangedBaselineStopsPublication` drive real writer boundaries.
`SuccessfulMaintenancePreservesCompleteHistoryAndReopens` compares retained rows, including undone work on protected entries.

**Build and run commands:** Build ProjectMaintenanceApplyTest, ProjectPackageBackendTest, and the
affected project lifecycle/task targets. Run targeted discovered cases. Run package/filesystem
replacement cases on Windows and macOS; report an unavailable platform honestly.

**Exit criteria:** [ ] Backup exists and verifies before every mutation. [ ] Failure injection covers
each publication/activation boundary. [ ] Retained-row and reopen evidence passes. [ ] Shared admission
blocks conflicting UI/automation writes. [ ] The UI enables only the complete operation.

**Expected diff:** 1,000-1,800 lines. Split before execution if lifecycle changes can exceed 2,000.

**Completion record:** Not started. Fill section 11 during execution.

### PM5 - Commands, caller coverage, and real product qualification

**Objective and deliverables:** Complete UI and command parity. Qualify selected removal on copied
real projects and record persistence, cancellation, accessibility, and performance evidence.

**Inputs and prerequisites:** PM4 complete operation and all earlier owner tests passing.

**Modules, files, and APIs:** Automation project commands/task tracker; maintenance model/controller;
translations, CMake registrations, VI documentation, and direct caller test fixtures.

**Data rules and invariants:** Commands use the same application service and explicit selected IDs.
No command implicitly selects/removes all findings or bypasses preservation/backup.

**Implementation steps:**

1. Add scan, paged result, and remove-selected command schemas with the shared service.
2. Connect task results and exact cancellation/error states to UI and command clients.
3. Exercise scan-only, user selection changes, empty selection, protected selection, and repeat scans.
4. Verify retained history, masks, album memberships, and package counters after reopen.
5. Review UI appearance, keyboard access, long paths, both themes, and narrow windows.
6. Record bounded pages/memory and scan/apply stage durations on a representative library.

**Primary success call chain:** UI or command -> shared service -> completed outcome -> consistent result.

**Primary failure and restore call chain:** schema/selection/owner failure -> exact rejection or PM4
restore result; application and command task states agree.

**Tests and evidence:** Extend AutomationProjectCommandsTest and the maintenance owner/model targets.
`MaintenanceCommandsCannotBypassProtectedRows` asserts the same protection result as UI calls.
`ScanCommandDoesNotMutateProject` checks ordered row digests.
`RemoveSelectedCommandRequiresExplicitIds` rejects empty and implicit-all requests.
`SecondMaintenanceScanFindsNoRemovedDuplicateOrMissingEntries` checks a successful repeat run.
`TaskResultCountsEqualActualRemovedRows` compares published counts with reopened storage.
`ProjectCloseWaitsForApplyAndCancelsScan` exercises both shutdown policies.
Manual checks must include selecting while a different album is open and keeping hidden selected
rows visible in the final confirmation count. Use copies of user data; never alter Photos for a test.

**Build and run commands:** Build alcedo_main, alcedo_cli, AutomationProjectCommandsTest, and the
maintenance targets. Run targeted command/owner/caller cases and the manual checks in section 10.

**Exit criteria:** [ ] UI and commands have identical preservation/removal behavior. [ ] Empty,
cancel, failure, recovery, and repeat-run checks pass. [ ] Manual VI record and actual counts exist.
[ ] No imported detection feature or full test suite was required to qualify maintenance.

**Expected diff:** 700-1,300 lines.

**Completion record:** Not started. Fill section 11 during execution.

## 9. Acceptance matrix

| Behavior | Expected observable result |
| --- | --- |
| Equal filenames in different folders | Separate groups; neither entry is removed for that reason |
| Equivalent full-path spelling | One group with full directory identity preserved |
| Unrated repeated import with no work | Eligible extra; designated existing-path survivor stays |
| Rated-only, edited-only, or rated-and-edited entry | Protected in both UI and service |
| Inactive Version or undone commit | Protected; complete retained history survives reopen |
| Missing source with protected work | Shown as missing and protected; cannot be selected |
| Missing pristine source | Eligible only after completed scan and explicit user selection |
| Permission failure or disconnected volume | Report-only source problem; not a missing-removal candidate |
| Broken DB/history/asset reference | Exact finding and protection; no guessed ownership repair |
| Selected album differs from project root | Whole-project scope and all relevant memberships are used |
| Source/status/rating/history changes after review | Fresh validation rejects unsafe selected operation |
| Batch contains chains, cycles, or a bad ID | Zero removal across the entire batch |
| Owner transaction failure | Staged rollback; active package and backup remain intact |
| Backup, prepublication, or failed atomic-replacement operation | No success; current original unchanged and no backup restore |
| Changed baseline before publication | Refuse publication; never restore the older backup over the new file |
| Published file changed externally before recovery | Leave current file untouched; preserve backup and report recovery required |
| Activation/restore failure | Exact error, preserved backup, and disabled conflicting writes |
| Existing/overlapping task or automation writer | Shared admission settles/refuses it deterministically |
| User cancels scan or hides dialog | No deletion; task/result behavior matches section 2 |
| Successful removal and reopen | Exact selected IDs gone; retained rows/assets unchanged |
| Repeat scan and removal | No removed IDs reappear; empty selection changes nothing |

## 10. Build and evidence rules

Plan creation runs no product build, test suite, or project operation.
The commands below are for implementation. Proposed test targets must be registered first.

```powershell
Set-Location C:\Users\Haidong\Data\Photography\AlcedoStudio
cmd /c scripts\msvc_env.cmd --preset win_debug -DCMAKE_PREFIX_PATH="D:/Qt/6.9.3/msvc2022_64/lib/cmake"
cmd /c scripts\msvc_env.cmd --build --preset win_debug --target ProjectMaintenanceServiceTest ProjectMaintenanceStorageTest ProjectMaintenanceApplyTest --parallel 4
$env:PATH = "$PWD/build/debug/vcpkg_installed/x64-windows/debug/bin;$env:PATH"
ctest --test-dir build/debug -N -R 'ProjectMaintenance|SourceImagePathTest'
ctest --test-dir build/debug --output-on-failure -j 1 -R 'ProjectMaintenance|SourceImagePathTest'
```

Each phase also builds its named direct-caller targets. Select actual discovered cases when a
target's CTest prefix differs from its target name. Use only win_debug/win_release on Windows and
the repository wrapper. Allow at least 10-20 minutes per build invocation; retain healthy builds.
Do not run a build and test concurrently in one build directory. Add the vcpkg runtime bin path.
Check copied runtime DLLs if an unchanged test executable appears to load old owner behavior.

On macOS use the fixed macos_debug directory and configure ALCEDO_BUILD_TESTS=ON when needed.
Verify source path comparison, filesystem errors, package replacement, and restore on that platform.
These checks require no GPU decode or render. Report unavailable platforms and skipped checks as such.
Never start the full test suite unless the user explicitly requests it. Do not use unreliable
WorkspaceShellTest synthetic input as the acceptance basis. Prefer C++ owner/model/command tests
and record manual QML behavior and appearance separately.

Use build/tmp/project_maintenance/<phase>/ for temporary logs and evidence. New files use LF.
Apply the separate line-ending conversion rule before editing existing CRLF source files. Include
the defining headers, preserve direct owner mutation, and format only changed existing lines.
Review all new/touched terminology and the full roadmap filename set before phase completion.

Before implementation, search existing GitHub items and record required issues under CONTRIBUTING.md.
The parent coordination plan records current issue/authentication status. An absent issue link is
not implementation evidence. Do not post AI follow-up conversations on existing issue threads.

### Completed Photos operation: evidence context only

The user separately approved and completed the one-time Photos cleanup on 2026-11-09:

- Removed 4,778 reviewed entries; 2,547 remained.
- Preserved all 70 rated and 97 edited entries, 4,080 commits, 2,645 Versions, and 47 mask assets.
- Compared complete retained row/asset digests and independently checked references and full paths.
- Reopened the cleaned package in Alcedo before publishing it.
- Preserved `C:\Users\Haidong\Documents\Alcedo\Photos.backup-before-cleanup-2026-11-09.alcd`.

That operation used the existing application deletion API on an isolated working package, followed
by independent validation and atomic replacement. It required operational orchestration that the
new feature must own. It does not mark PM1-PM5 implemented, and it does not replace failure-injection
or UI coverage. Do not rerun destructive operations on Photos to qualify this feature.

## 11. Completion record

Each phase remains planned until its owner/tests/product evidence is recorded.
Use this empty template during execution:

```text
Phase / date / status:
Source revision and branch:
Actual changed modules and changed-line count:
Implemented behavior and implementation decisions with reasons:
Primary success call chain:
Primary failure and restore call chain:
Build/test commands and exit codes:
Discovered / passed / failed / skipped counts:
Manual QML/accessibility/appearance verification:
Backup/package/activation and retained-row evidence:
Performance and resource evidence:
Evidence path and unavailable platforms:
Related issues and remaining defects:
```

Stop and update this plan before continuing if the source owners differ, a phase can exceed 2,000
changed lines, a new persisted format has no compatibility behavior, protected work can be selected,
or rollback/activation cannot retain the backup and report its real outcome. Resolve implementation
details within the approved decisions; do not defer them to an unfinished final-report decision list.
