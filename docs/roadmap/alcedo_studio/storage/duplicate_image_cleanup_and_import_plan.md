# Import Duplicate Skipping and Project Maintenance

Created: 2026-11-08

Updated: 2026-11-09

Status: both product features are planned. Product implementation has not started.
The separate one-time cleanup of Photos is complete.

Source audit revision: `7bcac45d`.

This document coordinates two independent feature plans:

- [Import Duplicate Skipping](import_duplicate_skipping_plan.md).
- [Project Maintenance](project_maintenance_plan.md).

The feature plans contain detailed phases, API changes, tests, commands, and completion records.
This document records shared decisions, dependency limits, and historical evidence.
The earlier combined D1-D6 sequence is superseded. Do not execute its former phases.

## 1. Approved product decisions

### 1.1 Import duplicate skipping

Use the normalized full source image path to select duplicate candidates. Preserve every folder
component. Different source paths remain separate images, even when their names and bytes match.

Within one matching path, compare file size in bytes first. Compare SHA-256 only when sizes match.
Store the source size and digest during each successful new import. This gives later comparisons
an import-time value. Hashing a legacy file later cannot reconstruct its historical bytes.

| Comparison | Required result |
| --- | --- |
| Different normalized full source paths | Import as separate images |
| Same path, same byte size, same recorded SHA-256 | Skip the duplicate |
| Same path, different byte size or recorded SHA-256 | Report a changed source; do not silently skip |
| Same path, no recorded identity | Establish an explicitly identified current-source legacy value, without a historical claim |
| Missing or unrecorded entry at another path | Do not block an unrelated import |

The option is enabled by default. Put its visible control at
**Settings > Default Behavior > Import > Skip duplicate imports**.
Use the Settings dialog's pending-value, Apply, and Cancel behavior.
Read the preference when a job starts. A later setting change affects subsequent jobs.
An explicit automation option overrides the preference for that request.

Keep the native file and folder selectors. Do not add a duplicate-review step to import.
The existing folder scan and file-type confirmation remains part of folder import.
File selection continues directly into import.

Check existing entries across the complete project and the current import batch. Confirm a skip
only against an entry whose import completed successfully. Count duplicates independently of
failures. Preserve existing edits, ratings, Versions, and album memberships.

### 1.2 Project Maintenance

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
| Import duplicate skipping | ImportService owns admission, source identity, and checked finalization. Import preferences own the setting. Existing UI and automation adapters pass request options. | [Import phases](import_duplicate_skipping_plan.md) | planned |
| Project Maintenance | The host-owned service and controller own scan, findings, selection, backup, checked removal, and publication. The controller coordinates project activation. Storage and history owners perform focused operations. | [Maintenance phases](project_maintenance_plan.md) | planned |

Neither feature requires the other feature's UI or database schema to ship.
Maintenance does not need an import fingerprint table to identify repeated full paths.
Import skipping does not need a maintenance scan or removal workflow.

Share one proposed pure path utility:

- Header: `alcedo_studio/src/include/utils/file/source_image_path.hpp`.
- Implementation: `alcedo_studio/src/utils/file/source_image_path.cpp`.
- Operation: `NormalizeSourceImagePath(const image_path_t&)` returns the full-path comparison value.
- Test target: proposed `SourceImagePathTest`.

The utility accepts an absolute path. It normalizes equivalent separator and dot spellings.
Windows comparison follows the agreed path case rules. macOS comparison preserves case in this
feature. The pure helper performs no filesystem case probing. Do not resolve symlinks or use inode
identity to merge different user-visible paths. Never discard folder components or compare only
a basename.

The first feature implemented introduces this utility and its tests. The other feature reuses them.
This shared prerequisite does not create a dependency on either complete feature.
Do not duplicate the normalizer in QML, import, storage, or the maintenance controller.

### 2.1 Phase map

The detailed feature documents are authoritative for phase work and completion evidence.

| Feature phase | Result | Expected changed lines | Status |
| --- | --- | ---: | --- |
| I1 | Shared full-path identity and source fingerprint persistence | 1,100-1,700 | planned |
| I2 | Checked finalization owned by ImportService | 1,000-1,700 | planned |
| I3 | Path, size, and SHA-256 duplicate admission with correct counts | 1,100-1,900 | planned |
| I4 | Settings preference and UI/automation option propagation | 850-1,500 | planned |
| I5 | Import integration, failure, and resource qualification | 650-1,100 | planned |
| PM1 | Read-only full-path scan and preservation evidence | 900-1,500 | planned |
| PM2 | Review model, background tasks, selection, and sidebar launcher | 1,100-1,800 | planned |
| PM3 | Explicit project-scope storage removal and rollback invariants | 1,100-1,900 | planned |
| PM4 | Verified backup, staged publication, activation, and restoration | 1,000-1,800 | planned |
| PM5 | Maintenance integration, automation, and manual qualification | 700-1,300 | planned |

Each estimate includes production code, tests, resources, build registration, and documentation.
Split a phase before its expected diff can exceed 2,000 lines. Record actual changed lines during
execution. A planning estimate is not completion evidence.

## 3. Verified source facts

Paths in this table are relative to `alcedo_studio/`.

| Area | Source | Verified behavior and planning consequence |
| --- | --- | --- |
| Import creation | `src/app/import_service.cpp:275` | Creates a new library entry and Image. Add path-aware checking before placeholders. |
| UI filtering | `src/ui/alcedo_main/album_backend/import_export.cpp:190` | Removes repeated raw path strings in one request. Equivalent Windows spellings can remain. |
| Import finalization | `src/app/import_service.cpp:440` | Image, root, and filesystem writes finish separately. The image-pool sync result is not checked. |
| Existing checksum | `src/image/image.cpp:302` | Hashes Image object memory. It is unsuitable for source identity. |
| Image mapping | `src/include/storage/mapper/image/image_mapper.hpp:63` | Uses positional mapping. Keep optional fingerprint persistence separate. |
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

This update changes documentation only. Do not start implementation, database cleanup, or a build.

Before executing a feature phase, reread AGENTS.md, CONTRIBUTING.md, the feature plan,
alcedo-msvc-cmake, and alcedo-qml-ui. Read DESIGN.md and the per-file VI catalog before UI work.
Confirm that the current source still agrees with the plan.

Use existing owners and focused changes. Do not create Image, metadata, or history mirrors.
The maintenance plan documents the independent package data needed for backup and publication.
Use defining includes and the repository terminology. New files use LF. Convert any affected
legacy CRLF file in a separate commit before content edits. Include conversion work in phase-size
review and split it when necessary. No conversion or commit is part of this plan update.

Use supported presets and fixed build directories. Start Windows build allowances at 10-20 minutes.
Keep temporary evidence under build/tmp/. Run only affected tests and direct callers.
Only the user may request a full test-suite run. Do not rely on WorkspaceShellTest synthetic input.
Record unavailable platforms and skipped tests honestly.

GitHub CLI remains unauthenticated. Issue search, maintainer verification, issue creation, and
roadmap-project updates could not run. No issue or comment was posted. This does not block the
authorized documentation update. Before implementation, follow CONTRIBUTING.md to track the two
separate features and the verified import/deletion persistence defects. Do not let an AI continue
a discussion on an existing issue.

After execution, update each feature's phase record with call chains, modules, changed lines,
targeted commands, pass/fail/skip counts, manual evidence, platform limits, and issue numbers.
Do not mark a phase complete without that evidence.
