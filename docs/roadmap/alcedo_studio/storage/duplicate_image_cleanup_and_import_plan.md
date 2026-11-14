# Import Duplicate Skipping and Project Maintenance

Created: 2026-11-08

Updated: 2026-11-10

Status: Import Duplicate Skipping I1-I5 is implemented and verified on Windows.
Project Maintenance PM1-PM5 is implemented and qualified on Windows; platform and native review limits are recorded in its feature plan. The separate one-time cleanup of Photos is complete.

Planning source audit revision: `7bcac45d`. Import implementation started from `d83f09c0`
on branch `de_duplicate`; see section 7 and the detailed phase completion records.

This document coordinates two independent feature plans:

- [Import Duplicate Skipping](import_duplicate_skipping_plan.md).
- [Project Maintenance](project_maintenance_plan.md).

The feature plans contain detailed phases, API changes, tests, commands, and completion records.
This document records shared decisions, dependency limits, and historical evidence.
The earlier combined D1-D6 sequence is superseded. Do not execute its former phases.

The 2026-11-10 revision uses path-and-size import comparisons and protects color labels and
deletion marks during maintenance. The Release app and targeted Windows/CUDA tests passed.
See [current qualification](import_duplicate_skipping_plan.md#12-current-qualification-2026-11-10).

## 1. Approved product decisions

### 1.1 Import duplicate skipping

Use the normalized full source image path to select duplicate candidates. Preserve every folder
component. Different source paths remain separate images, even when their names and bytes match.

Within one matching path, compare only the current and recorded byte sizes. Do not hash file
contents or store source hashes. On a changed size, preserve IDs, edits, ratings, color labels,
deletion marks, masks, Versions, and memberships. Record the new size, notify the user, and refresh
affected previews/editor content. The user accepts undetected same-size changes.

| Comparison | Required result |
| --- | --- |
| Different normalized full source paths | Import as separate images |
| Same path and recorded byte size | Skip the duplicate |
| Same path and different recorded byte size | Reuse the entry with its existing edits and ratings; record the new size |
| Same path without a recorded size | Record the current readable size and skip |
| Missing or unrecorded entry at another path | Do not block an unrelated import |

The owner confirmed that Photos.alcd already uses size-only storage. The hash prototype was not
merged upstream. Do not retain prototype hash migration or cleanup on project opening.

The option is enabled by default. Put its visible control at
**Settings > Default Behavior > Import > Skip duplicate imports**.
Use the Settings dialog's pending-value, apply, and dismiss behavior. The current dialog labels
apply as Done and uses Escape to discard pending edits. Settings is also reachable from Welcome.
Read the preference when a job starts. A later setting change affects subsequent jobs.
An explicit automation option overrides the preference for that request.

Keep the native file and folder selectors. Do not add a duplicate-review step to import.
The existing folder scan and file-type confirmation remains part of folder import.
File selection continues directly into import.

Check existing entries across the complete project and the current import batch. Confirm a skip
only against an entry whose import completed successfully. Count duplicates independently of
failures. Preserve existing edits, ratings, Versions, and album memberships.

### 1.2 Project Maintenance

Color labels and deletion marks are owner-managed user work. Protect them during scan and removal,
including marks added after review. Preserve their exact values on surviving entries after reopen.

Deliver Project Maintenance as a separate product feature. Add its text button directly below
**Background Tasks** in the top-left Collections sidebar. Open one application-owned review dialog.
A text button avoids assigning an unapproved SVG asset.

```text
Project Maintenance -> Scan -> Review findings and selection
  -> Remove selected -> Verified backup -> Checked removal and save -> Result
```

Scan the complete project, independent of the selected album. Identify repeated full source paths,
missing source files, access/read errors, and invalid project references. Give each finding a
specific reason. An access error is not proof that a file is missing.

Show full paths, entry IDs, ratings, editing evidence, and proposed retained entries. Let the user
select and deselect eligible removals. Keep every rated or edited entry protected, including
inactive Versions and undone commits. A missing source does not remove that protection.
Missing or inconsistent history evidence also prevents automatic removal.

Scanning does not delete data. Removing selected entries requires explicit review and confirmation.
Create and verify a backup before mutation. Removal affects project entries, not original image
files. Preserve retained settings, annotations, complete histories, masks, and album memberships.
Publish success only after checked persistence and package verification.

Expose scan progress and cancellation through Background Tasks. Use the existing interaction
policy for conflicting work. Revalidate selected IDs and preservation evidence before mutation.
Do not add a generation, version counter, or a new stale-result protocol.

## 2. Separate ownership and dependencies

| Feature | Responsibilities | Detailed plan | Status |
| --- | --- | --- | --- |
| Import duplicate skipping | ImportService owns admission, source identity, and checked finalization. Import preferences own the setting. Existing UI and automation adapters pass request options. | [Import phases](import_duplicate_skipping_plan.md) | implemented; Windows verified |
| Project Maintenance | The host-owned service and controller own scan, findings, selection, backup, checked removal, and publication. The controller coordinates project activation. Storage and history owners perform focused operations. | [Maintenance phases](project_maintenance_plan.md) | implemented; Windows verified |

Neither feature requires the other feature's UI or database schema to ship.
Maintenance does not need the import size table to identify repeated full paths.
Import skipping does not need a maintenance scan or removal workflow.

Import now provides the shared pure path utility:

- Header: `alcedo_studio/src/include/utils/file/source_image_path.hpp`.
- Implementation: `alcedo_studio/src/utils/file/source_image_path.cpp`.
- Operation: `NormalizeSourceImagePath(const image_path_t&)` returns the full-path comparison value.
- Test target: `SourceImagePathTest`.

The utility accepts an absolute path. It normalizes equivalent separator and dot spellings.
Windows comparison follows the agreed path case rules. macOS comparison preserves case in this
feature. The pure helper performs no filesystem case probing. Do not resolve symlinks or use inode
identity to merge different user-visible paths. Never discard folder components or compare only
a basename.

Project Maintenance reuses this implementation and its tests.
This shared prerequisite does not create a dependency on either complete feature.
Do not duplicate the normalizer in QML, import, storage, or the maintenance controller.

### 2.1 Phase map

The detailed feature documents are authoritative for phase work and completion evidence.

| Feature phase | Result | Expected changed lines | Status |
| --- | --- | ---: | --- |
| I1 | Shared full-path comparison and source-size persistence | 1,100-1,700 | complete on Windows |
| I2 | Checked finalization owned by ImportService | 1,000-1,700 | complete |
| I3 | Path-and-size duplicate admission with correct counts | 1,100-1,900 | complete on Windows |
| I4 | Settings preference and UI/automation option propagation | 850-1,500 | complete |
| I5 | Import integration, failure, and resource qualification | 650-1,100 | complete on Windows; platform limits recorded |
| PM1 | Read-only full-path scan and preservation evidence | 900-1,500 | complete on Windows; limits recorded |
| PM2 | Review model, background tasks, selection, and sidebar launcher | 1,100-1,800 | complete on Windows; limits recorded |
| PM3 | Explicit project-scope storage removal and rollback invariants | 1,100-1,900 | complete on Windows; limits recorded |
| PM4 | Verified backup, staged publication, activation, and restoration | 1,000-1,800 | complete on Windows; limits recorded |
| PM5 | Maintenance integration, automation, and manual qualification | 700-1,300 | complete on Windows; limits recorded |

Each estimate includes production code, tests, resources, build registration, and documentation.
Split a phase before its expected diff can exceed 2,000 lines. Record actual changed lines during
execution. A planning estimate is not completion evidence.

## 3. Verified source facts

These are the facts at the planning revision. The completed import changes are recorded in
[Import execution evidence](import_duplicate_skipping_plan.md#11-execution-evidence-2026-11-09).

Paths in this table are relative to `alcedo_studio/`.

| Area | Source | Verified behavior and planning consequence |
| --- | --- | --- |
| Import creation | `src/app/import_service.cpp:275` | Creates a new library entry and Image. Add path-aware checking before placeholders. |
| UI filtering | `src/ui/alcedo_main/album_backend/import_export.cpp:190` | Removes repeated raw path strings in one request. Equivalent Windows spellings can remain. |
| Import finalization | `src/app/import_service.cpp:440` | Image, root, and filesystem writes finish separately. The image-pool sync result is not checked. |
| Existing checksum | `src/image/image.cpp:302` | Hashes Image object memory. It is unsuitable for source identity. |
| Image mapping | `src/include/storage/mapper/image/image_mapper.hpp:63` | Uses positional mapping. Keep optional source-size persistence separate. |
| Settings | `src/ui/alcedo_main/qml/SettingDialog.qml:621` | Default Behavior exists. Reset and Apply already stage settings changes. |
| Import dialogs | `src/ui/alcedo_main/qml/AppDialogs.qml:41` | File selection starts import. Folder selection opens the existing scan/type dialog. |
| Sidebar | `src/ui/alcedo_main/qml/CollectionsPanel.qml:20` | The utility list contains Search and Background Tasks. Maintenance follows Background Tasks. |
| History ownership | `src/edit/history/pipeline_document_checkpoint.cpp:105` | A root ID includes its element ID. Preserve survivor IDs and history. |
| Edited indicator | `src/include/storage/store/sleeve/album_edit_state_sql.hpp:29` | Reads only the active Version. Maintenance must inspect all history and pending edits. |
| Existing removal | `src/ui/alcedo_main/album_backend/image_controller.cpp:873` | Runs several owner operations and suppresses some errors. Maintenance needs checked failure handling. |
| Delete scope | `src/include/ui/alcedo_main/album_backend/library_mutation_operations.hpp:43` | Depends on the selected folder. Maintenance needs explicit project scope. |
| History deletion | `src/storage/store/edit_history/commit_graph_store.cpp:555` | Owns a transaction. Atomic removal needs a transaction-neutral owner helper. |

Related plans:

- [Library search and project size](library_search_and_project_size_plan.md): import persistence,
  metadata, and storage. Extend current owners without repeating its destructive format cutover.
- [Album sorting and import time](../ui/album_sort_group_and_import_time_plan.md): preserve retained
  import times and album query behavior.
- [Background tasks](../ui/background_tasks_ui_state_plan.md): reuse the current task registry and
  interaction policy. Historical source descriptions do not override current source.
- [History identity](../ui/editor_single_live_pipeline_wal_checkpoint_plan.md): preserve the active
  identity rules. Its superseded executor ownership does not apply.
- [CLI and automation](../automation/cli_mcp_automation_plan.md): UI and automation use the same
  owner operations. The maintenance feature is not limited to a CLI operation.

## 4. Historical Photos investigation and completed cleanup

This section records actual user-project work. It does not establish product implementation or
automated test coverage for either feature.

### 4.1 Initial read-only audit, 2026-11-08

The first package had 7,332 image entries and 4,234 commits. Exact stored path strings identified
1,493 repeated paths and 2,955 entries above one per path. That comparison did not normalize slash
spelling. Its preliminary removal estimate was not an approved removal list.

It found edited copies of `_DSC8924.ARW`, `_DSC8933.ARW`, and `_DSC0103.ARW`.
The user then cleaned those three groups manually. Do not reuse their initial survivor IDs.

### 4.2 Reviewed removal list, 2026-11-09

The later saved project had 7,325 entries. Windows path normalization identified 2,819 paths.
The filesystem confirmed 1,557 groups with equivalent slash spellings. Sixteen filenames also
occurred in different folders; their full paths remained separate.

The user reviewed and approved 4,778 removals:

- 4,183 redundant entries with readable sources.
- 595 entries belonging to 272 missing source paths.

Every removal was unrated and had no stored editing work. All protected entries remained.
The package changed after review, so the approved IDs were revalidated before removal.
The later save did not change their eligibility.

### 4.3 Completed operation

The original was backed up first. Alcedo's existing deletion API operated on an isolated working
package. It reported exactly 4,778 deleted IDs and zero failed IDs. Independent database checks
compared retained rows. The cleaned package reopened in Alcedo before publication to
`C:\Users\Haidong\Documents\Alcedo\Photos.alcd`.

| Result | Verified value |
| --- | ---: |
| Remaining image entries | 2,547 |
| Retained rated entries | 70 |
| Retained edited entries | 97 |
| Retained commits | 4,080 |
| Retained Versions | 2,645 |
| Retained mask assets | 47 |
| Repeated normalized full paths remaining | 0 |
| Missing source paths remaining | 0 |
| Changed retained-table hashes | 0 of 11 |
| Reference check failures | 0 |

Backup: `C:\Users\Haidong\Documents\Alcedo\Photos.backup-before-cleanup-2026-11-09.alcd`.

Backup SHA-256: `84a7c2f0ed8a9724ed7e4dbe77a9a8ab25e153b02c518f4677a26e5cbe4eab40`.

Cleaned SHA-256: `6c52eacfced307c60fc60287d3e325e78e151986fc1403759e220b234e443a1e`.

Saved artifacts: `photos-cleanup-review.html`, `photos-cleanup-review.json`, and
`photos-cleanup-result.json`, in this chat's durable visualization directory. They are local operation
evidence, not repository fixtures. Task-specific temporary files were removed. No original images
were deleted. Product phases below remain planned despite this completed operation.

## 5. Superseded proposals

Do not restore these former choices:

- Content matching across different full source paths.
- Treating a copied or renamed file at another path as the same project image.
- New duplicate controls or review dialogs in the import flow.
- An unrecorded entry at another path blocking an unrelated import.
- A CLI-only maintenance feature.
- Describing Photos cleanup as pending or using it as product completion evidence.
- Deleting rated or edited entries because their source is missing.

## 6. Execution and completion rules

The planning update authorized documentation only. The user subsequently authorized Import Duplicate
Skipping implementation on 2026-11-09. This authorization covers I1-I5 and their builds/tests; it
did not authorize Project Maintenance implementation or another cleanup of Photos. The user
subsequently authorized PM1-PM5 implementation on 2026-11-09, including targeted builds/tests
and qualification on disposable copies. Another cleanup of Photos remains outside this scope.

Before executing a feature phase, reread AGENTS.md, CONTRIBUTING.md, the feature plan,
alcedo-msvc-cmake, and alcedo-qml-ui. Read DESIGN.md and the per-file VI catalog before UI work.
Confirm that the current source still agrees with the plan.

Use existing owners and focused changes. Do not create Image, metadata, or history mirrors.
The maintenance plan documents the independent package data needed for backup and publication.
Use defining includes and the repository terminology. New files use LF. Convert any affected
legacy CRLF file in a separate commit before content edits. Include conversion work in phase-size
review and split it when necessary. The implementation's prerequisite LF conversion is recorded
under the import phase evidence.

Use supported presets and fixed build directories. Start Windows build allowances at 10-20 minutes.
Keep temporary evidence under build/tmp/. Run only affected tests and direct callers.
Only the user may request a full test-suite run. Do not rely on WorkspaceShellTest synthetic input.
Record unavailable platforms and skipped tests honestly.

GitHub CLI was unauthenticated during planning. During implementation the user confirmed that it
is authenticated and identified `origin` as their fork, with `upstream` outside the permitted remote
scope. Read-only verification confirmed ClearSights administrator access to `ClearSights/AlcedoStudio`
and `hasIssuesEnabled=false`. Issues cannot be searched or created on that fork while they are
disabled. No issue or comment was posted, and upstream was not modified. Do not enable Issues or
write to upstream as a substitute. The detailed import record identifies the fixed owner-lock defect
and this tracking limitation. Do not let an AI continue a discussion on an existing issue.

After execution, update each feature's phase record with call chains, modules, changed lines,
targeted commands, pass/fail/skip counts, manual evidence, platform limits, and issue numbers.
Do not mark a phase complete without that evidence.

## 7. Import implementation completion (2026-11-09)

Historical hash-prototype evidence follows. The approved size-only behavior in section 1.1 and
the current import feature plan supersede this earlier source-identity design and API list.

I1-I5 are implemented. The default-on setting applies to file, folder, drop, and automation requests.
An explicit automation boolean overrides the saved preference for one request. Native file/folder
selection and folder scan/type confirmation retain their existing flows. Welcome now exposes a
Settings text action so the application-wide preference is editable before entering a project.

```text
request -> saved preference or explicit override -> ImportService submission owner
  -> normalized full path -> byte size -> SHA-256
  -> persisted same-path match: confirmed duplicate skip
  -> new independent entry: metadata -> checked Image/filesystem/root/fingerprint writes
  -> result counters -> existing package save and UI/automation completion

source/change/write error -> explicit occurrence error -> provisional removal through owners
  -> checked restoration or explicit restoration failure and stopped admission
```

The implementation uses bounded 64-entry batches and finalizes a pending representative before
deciding a repeated occurrence. It preserves every different source path and all accepted user
state. Source fingerprints are additive optional data with explicit import/current-source basis.

The final Windows targeted selection has **74 selected, 73 passed, 0 failed, 1 skipped**. The
existing 1,100-distinct-hard-link capacity case and selected package byte-layout case passed.
Native Settings checks passed in both themes, with keyboard toggle, dismiss, save/reopen, Welcome
access, and a smaller window. macOS was unavailable; Windows symlink creation lacked privilege.
The conditional pipeline-document test lacked its required RAW fixture. The full suite was not run.

Actual feature work before roadmap records totals 2,646 changed lines, plus a separate 34-line
LF-only conversion in commit `6f4857f6`. Shared core changes are counted once across I2/I3.
Feature changes remain uncommitted. No remote push was made.

See [phase completion records](import_duplicate_skipping_plan.md#7-detailed-phases-historical-implementation-records) for owner APIs
and success/failure chains, and [execution evidence](import_duplicate_skipping_plan.md#11-execution-evidence-2026-11-09)
for exact commands, per-suite counts, source-I/O/memory measurements, file-size review, and limits.
Project Maintenance and the historical cleanup records are separate from this completion.

### Project Maintenance implementation completion (2026-11-09)

PM1-PM5 now provide whole-project scan, protected-work review, explicit selection, verified backup,
atomic staged removal, checked publication/reopening, and guarded restoration. UI and commands use
one application-owned controller. Windows verification passed 47 maintenance cases and 87 direct
caller cases, with four prerequisite-related skips. A copied real project completed backup, one
selected test-entry removal, and reopening; 2,576 entries remained and a repeat scan had no findings.
The original Photos package was unchanged. See [the completion evidence](project_maintenance_plan.md#11-completion-record)
for commands, preservation checks, native review, timing, and unavailable coverage.
