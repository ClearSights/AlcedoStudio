# Import Duplicate Skipping Plan

Date: 2026-11-09

Status: I1-I5 implemented and verified on Windows. macOS execution and privileged symlink
creation were unavailable; section 11 records these limits.

Planning source audit: `7bcac45d`. Implementation started from `d83f09c0` on branch `de_duplicate`.
Prerequisite commit `6f4857f6` converts the mixed import-log line endings to LF; feature changes
remain in the working tree.

Coordination parent: [Duplicate Import and Project Maintenance Plans](duplicate_image_cleanup_and_import_plan.md).

Primary owners: ImportService, Storage, ImportExportHandler, and
ImportBehaviorPreferences object.

## 1. Approved product decisions

Add duplicate skipping during import as a separate feature. This feature can ship without
Project Maintenance. The user authorized implementation on 2026-11-09. The earlier planning-only
restriction is superseded for I1-I5. Project Maintenance remains outside this implementation.

Use the complete source file path to select possible duplicates. Different full paths are separate
images, including identical files in different folders. A shared filename never establishes identity.
Within one normalized full path, compare the file size in bytes first. Compare SHA-256 only when
the byte sizes match. Report a changed source instead of skipping it when either comparison differs.

Add a default-on **Skip duplicate imports** setting at **Settings > Default Behavior > Import**.
Use the existing Settings Apply and Cancel behavior. Preserve the native file and folder pickers.
Preserve the existing folder scan and file-type confirmation. Do not add a duplicate-review step
or a duplicate checkbox to either import flow.

Apply the setting to file selection, folder import, dropped files, and automation. Read it when an
import starts. Changing Settings affects later imports. An explicit automation option overrides
the saved setting for that request only. Disabling skipping permits independent library entries
and independent edit histories.

Skipping must preserve every existing entry, including its rating, history, masks, source binding,
album memberships, and import time. Count duplicates as successful skips, separately from failures.
Detect repeated paths both in the existing project and in the current job.

This plan replaces the earlier proposals to merge content across different paths, to add per-import
confirmation controls, and to block unrelated imports because a legacy source is unavailable.

## 2. Product behavior

### 2.1 Matching rules

A normalized source path is an absolute lexical path used for comparison. Normalize separators and
dot segments. On Windows, compare case-insensitively. On macOS, preserve case in this feature.
The pure helper performs no filesystem case probing. Do not resolve symbolic links, hard links, or file
identities into one matching path. File identity may validate a read, but it is not a duplicate key.

| Existing binding versus incoming file | Result when skipping is enabled |
| --- | --- |
| Different normalized full paths | Import a separate entry, even when filenames, sizes, and hashes match |
| Same normalized full path and different recorded byte size | Report `SOURCE_CHANGED`; create no new entry and skip nothing |
| Same path and size, with matching recorded SHA-256 | Skip the incoming occurrence and preserve the existing entry |
| Same path and size, with different recorded SHA-256 | Report `SOURCE_CHANGED`; create no new entry and skip nothing |
| Same path, but no fingerprint exists for a legacy entry | Establish the explicit current-source baseline described below, then compare |
| An unrelated legacy source is missing or unhashed | Continue this incoming path's normal decision; unrelated paths cannot match |
| The incoming file cannot be read or changes during import | Report its real read or source-change error |

If several existing entries use the same path, read all their recorded fingerprints. Skip only when
the current source agrees with every recorded fingerprint. A disagreement reports a changed source
and leaves all existing entries intact. Import prevention does not select or delete an existing copy.

### 2.2 Stored size and digest

Record the source byte count and SHA-256 during every successful new import, including imports
with skipping disabled. This provides an import-time comparison value for later requests.
The size comparison avoids unnecessary digest comparisons and reads of unrelated stored entries.
It does not remove the initial full-file read needed to record a new source digest.

Use the proposed `ImageSourceFingerprint` table:

| Field | Meaning and validation |
| --- | --- |
| `image_id BIGINT PRIMARY KEY` | Existing Image owner; a live FileImage binding must exist before publication |
| `byte_size UBIGINT NOT NULL` | Number of source bytes read successfully |
| `sha256 VARCHAR NOT NULL` | Exactly 64 lowercase hexadecimal characters |
| `basis VARCHAR NOT NULL` | `import` or `legacy_current_source` |

Absence of a row means that identity is unrecorded. Do not store a zero or fabricated digest.
The table is optional for existing projects. Create it additively on fresh and existing database
opens. Preserve project format 0.10.0, existing metadata, and history. Keep any content index
nonunique because different paths and explicit independent copies are valid.

For a legacy same-path entry, compute a fingerprint from the currently referenced file through
the source owner. Store `basis=legacy_current_source`. This establishes a present-file baseline;
it does not prove that those bytes are the bytes originally imported. Expose that distinction in
the import log. Do not recreate a claim of historical identity from file size alone.

Initialize only legacy entries needed for the incoming path comparison. Do not scan or hash the
whole project before each import. A read failure on a needed same-path legacy source reports an
explicit comparison error. Missing or unhashed entries at other paths never block admission.

A saved fingerprint describes previously accepted bytes. An offline source does not erase that
record. An incoming readable occurrence of the same normalized path can compare against it.
Import does not relink an offline entry to a different path.

The fingerprint table contains new source identity information. It does not copy Image metadata,
paths, pipelines, or history. Read those values through existing owners. An owner operation that
replaces a source binding must invalidate its fingerprint in the same logical update.

### 2.3 Settings and progress

```text
Settings
  Default Behavior
    Import
      [x] Skip duplicate imports
      Skips repeated imports from the same source path.
      Files in different folders remain separate.

                                         Done
```

Add an Import section to the existing Default Behavior page. Use ThemeCheckBox and the shared
Settings section structure. Use AppTheme tokens and the current Settings typography. Labels wrap
at narrow widths. No new icon, badge, status dot, or palette is required.

Opening Settings loads the saved value into a pending boolean. Space toggles the focused checkbox.
Tab follows the existing dialog order. The existing Done action applies and persists the pending
value. Escape dismisses the dialog and discards it. Reopening Settings shows the saved value. The preference is application
wide and remains editable when no project is open.

Implementation inspection confirmed that the current Settings dialog labels Apply as Done and
uses Escape for cancellation. Keep those existing actions. The Welcome screen now has a Settings
text action so the preference is reachable before the user enters a project.

Persist `import/skipDuplicates`, default true, through a proposed ImportBehaviorPreferences owner.
Do not add import preferences to the editor-specific EditorBehaviorPreferences class. Expose the
new preference object through ApplicationModuleHost. Settings must not read QSettings directly.

At job creation, resolve the saved value or an explicit request override on the caller's owning
thread. Pass that scalar boolean in ImportOptions. This value belongs to that request. Workers
must not read QSettings or watch preference changes during a job.

Add `duplicates_` and the RAW subset needed by the current progress rings. Use these equations:

```text
processed = imported + duplicates + failed
requested = imported + duplicates + failed, after completion
```

Unsupported, excluded-type, and canceled counts remain documented subsets of failed. A duplicate
never increases failed. An all-duplicate request completes normally with zero imported and zero
failed. Show imported, duplicate-skipped, and failure counts as separate values. Use the existing
error presentation for real failures. Do not create an import-review dialog.

## 3. Current source audit

Paths in this table are relative to `alcedo_studio/`.

| Area | Current source fact | Required change |
| --- | --- | --- |
| Admission | `src/app/import_service.cpp:267-331` creates a file and Image before metadata work | Decide duplicate admission before creating a second placeholder |
| Metadata | `src/app/import_service.cpp:355` reopens the source on a metadata worker | Validate source stability across hash, metadata, and finalization |
| Completion | `TryFinishImportJob` reports after root batches complete | Publish final results after checked image, library, root, and fingerprint persistence |
| Sync | `src/app/import_service.cpp:469` ignores the image-pool sync status | Check every persistence outcome and report failures |
| Executors | `src/include/app/import_service.hpp` owns one submission executor and metadata workers | Reuse those owners; serialize admission and finalization |
| UI path filtering | `src/ui/alcedo_main/album_backend/import_export.cpp:140-210` compares raw path strings within one request | Preserve requested occurrences for owner accounting; use normalized matching in ImportService |
| UI finalization | `import_export.cpp:668-695` receives completion, then calls SyncImports | Move import persistence into ImportService and remove the second caller sync |
| Options/results | `src/include/app/import_service.hpp` has no duplicate fields | Add request flag, skip progress, skip results, and explicit comparison errors |
| Image checksum | `src/image/image.cpp:302` hashes C++ object memory | Do not use it as a source-file fingerprint |
| Storage | `src/storage/store/database.cpp:71` runs additional table DDL for fresh and existing databases | Add fingerprint DDL without changing the positional Image mapping |
| Preference page | `src/ui/alcedo_main/qml/SettingDialog.qml:621` contains Default Behavior | Add the Import section through pending/reset/Apply handling |
| Preference precedent | `src/include/ui/alcedo_main/album_backend/editor_behavior_preferences.hpp` owns editor settings | Add a small import-specific owner |
| Pickers | `src/ui/alcedo_main/qml/AppDialogs.qml:41-87` starts file imports directly and opens a folder scan dialog | Preserve these flows and pass the resolved option |
| Automation | `src/ui/alcedo_main/automation/commands/library_commands.cpp:507` registers library.import | Add optional `skip_duplicates`; omission inherits the saved preference |
| Task accounting | `src/ui/alcedo_main/automation/automation_task_tracker.cpp` counts imported plus failed | Include confirmed duplicate outcomes |

Existing ImportServiceTest registration lacks gtest discovery. ImportContentClassificationTest is
discoverable. ImportPipelineDocumentTest is built only when `ALCEDO_CUDA_ENABLED` is true.
ImportExportHandlerTest and AutomationLibraryCommandsTest cover direct callers. Confirm current
test registration again before executing any implementation phase.

Related plans:

- [Library search and project size](library_search_and_project_size_plan.md): existing import
  cleanup, small metadata, and storage boundaries. This feature extends import admission.
- [Album sorting and import time](../ui/album_sort_group_and_import_time_plan.md): preserve existing
  survivor import times and album query ownership.
- [CLI and shared-session automation](../automation/cli_mcp_automation_plan.md): use the existing
  command registry, task results, and the same application owners.
- [Project Maintenance](project_maintenance_plan.md): a separate feature that can reuse the pure
  path utility. Neither feature requires the other's UI or mutation workflow.

## 4. Owners, files, and APIs

All names marked proposed below are design names, not existing source claims.

| Owner | Current or proposed files | Input, output, and allowed mutation |
| --- | --- | --- |
| Pure path utility | Proposed `src/include/utils/file/source_image_path.hpp` and `src/utils/file/source_image_path.cpp` | `NormalizeSourceImagePath(const image_path_t&)` returns a full-path comparison value; no I/O mutation |
| File fingerprint utility | Proposed `src/include/utils/file/source_fingerprint.hpp` and implementation | Read an existing source using bounded buffers; return size, SHA-256, or a real read/change error |
| Fingerprint storage | Proposed `storage/store/image/source_fingerprint_store.*`, plus `sleeve/storage.*` and database DDL | Read/insert identity through short-lived Database connection guards; no complete Image copies |
| Source binding reads | `storage/store/image/image_store.*` and required mapper code | Find bound IDs for a normalized full source path through a scoped read; never expose mutable internals |
| ImportService | `app/import_service.*`, import log/error definitions | Own job admission, pending same-path decisions, cancellation, checked persistence, and completion |
| Import preference | Proposed `ui/alcedo_main/album_backend/import_behavior_preferences.*` | Own `skipDuplicateImports` bool and QSettings persistence for application lifetime |
| UI adaptor | `album_backend/import_export.*`, ApplicationModuleHost | Resolve request option once; publish progress and completion on the UI thread |
| Settings | `qml/SettingDialog.qml`, translations, UI CMake | Edit the pending preference; Apply calls the preference owner |
| Automation | `automation/commands/library_commands.cpp` and task tracker | Validate an optional boolean and return the same owner results as GUI imports |

The shared pure path utility may be introduced by this feature or by Project Maintenance first.
Reuse one implementation and SourceImagePathTest. This is a source dependency, not a dependency
on either product feature being complete.

Fingerprint reads return minimal identity values and bound IDs. These are new information and
ordinary operation results. Do not add an Image, CommitGraph, or pipeline mirror to perform lookup.
Do not keep long-lived database connections in new stores. Use the existing database lock at the
operation boundary. Include defining headers when adding first-party declarations.

ImportService owns one active job through finalization. Another ImportToFolder call receives an
explicit busy error until the first job finishes. Metadata and hashing remain worker operations.
Admission and finalization run through the existing submission executor. Completion is delivered
exactly once after all requested occurrences have a final outcome.

Required success chain:

```text
GUI request or library.import
  -> caller resolves preference or explicit override
  -> ImportService receives immutable request scalar
  -> normalize full path -> find same-path bindings
  -> compare byte size -> compare SHA-256
  -> confirmed existing match: successful skip
  -> new path: create one admitted entry -> metadata/root -> checked persistence
  -> store import-time fingerprint -> publish final result -> existing package save owner
```

Required failure chain:

```text
source read/change, needed legacy comparison, metadata, or storage failure
  -> explicit ImportError and failed occurrence
  -> remove provisional state through its owners
  -> finish only after restoration succeeds, or report restoration failure and stop admission
```

Do not invent a synthetic digest, change image quality, or try another backend after failure.

## 5. Executable ordering and source-read requirements

The current submission executor can reach a repeated path while its metadata worker is unfinished:

```text
submission admits A1 -> metadata worker starts A1
  -> submission reaches A2 with the same normalized source path
  -> A1 has no persisted Image/FileImage pair yet
```

A database-only lookup misses A1. Keep minimal current-job pending state keyed by normalized path.
The implemented submission owner finalizes the bounded batch before it decides a repeated pending
path. This preserves request order without a follower queue or copied Image state.
Only count A2 as a duplicate after a valid representative exists. If A1 fails, A2 has no confirmed
representative and must not disappear as a successful skip. Resolve each occurrence through the
same admission method and preserve A1's actual failure. A database failure is not a reason to retry
the transaction through every path occurrence.

There is also a current source reopening boundary:

```text
hash worker reads bytes A -> worker completes
  -> external writer replaces the file at the same path
  -> metadata extraction opens bytes B -> finalization writes A's digest beside B's metadata
```

Validate file identity and source observations across hashing, metadata extraction, and persistence.
Keep an open read handle where the platform reader supports it. Reject a replacement, write, size
change, or inconsistent final digest. The fingerprint and metadata must describe the same bytes.
Use file observations for read validation only; they never merge two distinct source paths.

Tests must drive these actual submission/worker and file-reopen boundaries. Do not add generations,
epochs, or cancellation protocols for hypothetical races. Reuse the existing cancellation request.

## 6. Phase summary

| Phase | Result | Main modules | Dependency | Expected diff | Status |
| --- | --- | --- | --- | ---: | --- |
| I1 | Full-path comparison and source fingerprint storage | utilities, Storage | None | 1,100-1,700 lines | complete on Windows |
| I2 | Checked import-owned finalization | ImportService and direct callers | I1 | 1,000-1,700 lines | complete |
| I3 | Size-then-SHA duplicate admission and accounting | ImportService, logs, results | I2 | 1,100-1,900 lines | complete on Windows |
| I4 | Default Behavior preference and request propagation | Settings, adaptor, automation | I3 | 850-1,500 lines | complete |
| I5 | End-to-end failure, persistence, and resource evidence | owner/integration tests | I4 | 650-1,100 lines | complete on Windows; platform limits recorded |

Estimates include source, tests, build registration, resources, and plan completion updates.
The split separates storage, lifecycle changes, admission, UI policy, and final qualification.
Split a phase before its expected diff can exceed 2,000 lines.

Before every phase, reread AGENTS.md, CONTRIBUTING.md, alcedo-msvc-cmake, and the applicable QML
skill. Recheck the source audit. No build or test command in this document ran during planning.

## 7. Detailed phases

### I1 - Full-path comparison and fingerprint storage

**Objective and deliverables:** add the shared pure path operation, bounded file hashing, and
optional fingerprint persistence. A reviewer can observe distinct folders remaining separate and
existing project data remaining intact after schema initialization.

**Inputs and prerequisites:** current Image/FileImage owners and database initialization. Reuse
the shared path implementation if Project Maintenance introduced it first.

**Modules, files, and APIs:** the proposed path and fingerprint utilities; proposed fingerprint
store; Storage accessors; database DDL; focused ImageStore source-binding reads; utility/storage
CMake and proposed SourceImagePathTest and SourceFingerprintTest targets.

**Data rules and invariants:** section 2.1 path scope; complete-file size/digest; explicit provenance;
one fingerprint per Image; nonunique content values; no copied metadata or source paths in the table.

**Implementation steps:**

1. Implement absolute lexical path comparison with explicit platform case rules.
2. Implement bounded streaming SHA-256 and source-change/read errors.
3. Add validated fingerprint DDL on both database-open paths.
4. Add focused scoped owner reads and validated insert/invalidation operations.
5. Initialize legacy same-path records with current-source provenance only when requested.
6. Register and discover behavior tests. Preserve the positional Image schema mapping.

**Success chain:** source owner -> full-path match -> file read -> validated fingerprint -> store.
**Failure chain:** path/read/schema/write error -> explicit failure -> no fabricated or partial row.

**Tests and observable assertions:**

- `WindowsSlashAndCaseSpellingsShareOneFullPath`: equivalent lexical paths compare equal.
- `DifferentFoldersRemainDifferentDespiteSameFilename`: directory components remain significant.
- `DifferentUserVisibleSymlinkPathsRemainSeparate`: matching does not resolve target identity.
- `MacOsCaseSpellingsRemainDistinct`: macOS lexical comparison preserves case without volume probes.
- `SourceDigestMatchesKnownSha256Bytes`: the complete fixture bytes produce the expected digest.
- `ReadFailureCreatesNoFingerprint`: an injected read failure leaves identity unrecorded.
- `ExistingProjectGainsOptionalFingerprintTableWithoutHistoryChanges`: all retained history rows agree.
- `LegacyFingerprintRecordsCurrentSourceBasis`: initialization makes no import-time claim.
- `UnrelatedMissingLegacyPathIsNotReadForCandidateLookup`: same-path lookup never hashes unrelated files.

**Build/run:** build SourceImagePathTest, SourceFingerprintTest, and ProjectPackageBackendTest using
section 9 commands. Discover their names, then run only those targets' affected cases.

**Exit criteria:**

- [x] Windows path cases and complete-byte digest assertions pass; macOS is unavailable.
- [x] Existing project reopen preserves metadata, ratings, history, and format identities.
- [x] Read/write failure stores no invalid fingerprint.

**Expected diff:** 1,100-1,700 lines.

#### I1 completion record (2026-11-09)

**Status:** complete on Windows. Utilities and fingerprint storage use 613 changed lines across
13 files, including utility tests and build registration. Integration assertions are counted under I5.

`NormalizeSourceImagePath` owns absolute lexical normalization and Windows invariant case folding.
`SourceFingerprintReader` streams SHA-256 with a 64 KiB buffer and holds the protected read handle
through metadata and persistence. `SourceFingerprintStore` owns scoped candidate reads and
validated identity transactions through `Database::GetConnectionGuard`. The existing Database
accessor is sufficient; no additional Storage forwarding API or Image copy was needed.
`ImageStore` invalidates identity when the source binding changes, in the same transaction, and
removes identity with its Image owner. The positional Image schema and project version are unchanged.

**Success chain:** absolute source -> normalizer -> scoped bound-ID lookup -> protected source read
-> validated fingerprint transaction -> persisted identity with explicit provenance.

**Failure chain:** invalid digest/provenance, absent binding, or source read error -> exception
-> no identity row; a failing batch rolls back its earlier inserts. Existing identity is never replaced.

**Executed evidence:** SourceImagePathTest, SourceFingerprintTest, and the I5 storage/reopen tests.
`LegacyCurrentSourceInitializationSurvivesReopenWithoutHistoricalClaim` drops the optional table,
reopens the existing database, compares Image/root/Version rows, and verifies explicit legacy basis.
`InvalidFingerprintOrMissingBindingCannotPublishPartialRows` checks validation, batch rollback,
and preservation of accepted identity. The selected ProjectPackageBackendTest checks the existing
package byte layout. Commands and totals are in section 11. Symlink creation and macOS execution
are recorded there as unavailable, not as passed.

### I2 - Checked import-owned finalization

**Objective and deliverables:** ImportService finishes persistence before it publishes success.
Its lifecycle works for service callers that do not use the UI adaptor.

**Inputs and prerequisites:** I1, current root-batch writer, ImagePoolService sync status, and all
ImportToFolder/SyncImports callers found in source and tests.

**Modules, files, and APIs:** `app/import_service.*`; focused image/library/root persistence owner
operations; `album_backend/import_export.*`; app test fixtures; app CMake.

**Data rules and invariants:** one active job through finalization; exactly one terminal result;
no successful Image/FileImage/fingerprint identity for an uncommitted representative; no sync of
unrelated state merely to perform a duplicate comparison.

**Implementation steps:**

1. Trace every direct caller and move checked finalization into ImportService's submission owner.
2. Check image-pool, library, root, and fingerprint persistence results explicitly.
3. Remove the adaptor's duplicate SyncImports call and update direct test callers together.
4. Reject overlapping jobs until finalization completes. Keep metadata workers parallel.
5. Restore failed provisional entries through owners before final completion.
6. Propagate restoration errors and stop further admission when restoration cannot complete.
7. Add gtest discovery for ImportServiceTest without starting the full repository suite.

**Success chain:** workers finish -> submission owner finalizes -> checked writes -> final callback.
**Failure chain:** owner rejects a write -> explicit failure -> provisional removal -> failed result;
restoration failure -> explicit terminal error with the affected IDs retained for diagnosis.

**Tests and observable assertions:**

- `CompletionRunsAfterImageLibraryAndRootPersistence`: a callback can read the saved live binding.
- `ImageWriteFailureCannotBeReportedAsImported`: imported count stays zero for the failed entry.
- `RootWriteFailureRemovesProvisionalBinding`: no failed Image, file, or root remains after restoration.
- `OverlappingJobIsRejectedUntilFinalizationCompletes`: an actual queued second request receives busy.
- `CanceledJobFinalizesCompletedEntriesExactlyOnce`: finished work persists and pending work is canceled.
- `RestorationFailureIsReturnedToTheCaller`: the real removal error is visible rather than swallowed.

**Build/run:** build ImportServiceTest, ImportContentClassificationTest, and ImportExportHandlerTest.
Include ImportPipelineDocumentTest only when its current CUDA condition is enabled. Use section 9.

**Exit criteria:**

- [x] Callback-order and write-failure assertions pass through the production owner path.
- [x] Direct callers need no later UI sync to finish an import.
- [x] Cancellation and busy behavior remain observable and deterministic.

**Expected diff:** 1,000-1,700 lines.

#### I2 completion record (2026-11-09)

**Status:** complete. I2 and I3 share one ImportService rewrite and its declarations/log changes:
894 changed lines across eight core files. Count this shared change once. The UI removal of the
second sync is included in I4's adapter count. Final integration tests are included in I5.

The submission executor owns bounded batches of 64 entries. Workers return source-read and root
encoding outputs while Image data remains in its pool owner. New focused
`ImagePoolService::PersistImportedImages` and `DiscardImportedImages` operations affect only the
admitted IDs. The existing root writer and filesystem owner remain responsible for their writes.
`SyncImports` remains a checked compatibility API; UI completion no longer calls it.

**Success chain:** metadata workers -> submission flush -> Image insert -> filesystem sync
-> root write -> fingerprint batch -> source validation -> success counts -> one terminal callback.

**Failure chain:** failed worker or persistence operation -> actual occurrence error -> remove
provisional fingerprint/history/files/Image through their owners -> checked completion. A restoration
error is returned explicitly and prevents subsequent admission on the affected service.

**Executed evidence:** callback reads all four persisted owners; image/library/root/fingerprint
failure tests assert restored rows; an overlapping request receives BUSY while finalization is
active. `CanceledJobFinalizesCompletedEntriesExactlyOnce` cancels after one confirmed entry,
asserts its complete persistence and exactly two canceled remaining occurrences, and compares
submitted/finished task counts. The earlier pre-canceled test is now named
`PreCanceledJobReportsEveryOccurrenceExactlyOnce` to state its narrower assertion.

Owner testing exposed the existing ImagePoolService Write -> SyncWithStorage recursive lock error.
Write now calls the private already-locked helper; public sync still acquires its lock. The rating
and source-binding update tests pass through that owner path. Issue tracking is unavailable as
recorded in section 11.

### I3 - Size-then-SHA admission and duplicate accounting

**Objective and deliverables:** implement the approved comparison table for existing entries and
same-job occurrences. Return correct skip and failure counts without adding duplicate placeholders.

**Inputs and prerequisites:** I2 checked completion, I1 source identity operations, current import
logs and error types, and supported-file classification.

**Modules, files, and APIs:** `app/import_service.*`; `utils/import/import_log.hpp` and error codes;
ImportProgress/ImportResult; focused storage operations; import service and content tests.

**Data rules and invariants:** different paths always remain separate; byte size precedes digest
comparison; changed sources fail explicitly; fingerprints for successful new imports have import
provenance; duplicate outcomes require a valid representative; progress equations in section 2.3.

**Implementation steps:**

1. Add default-on `ImportOptions::skip_duplicates_` and explicit source/comparison errors.
2. Perform normalized full-path lookup before placeholder admission.
3. Compare recorded byte size first. Compare SHA-256 only for same-path equal-size candidates.
4. Record a new import's digest and size during its successful import, including skip=false jobs.
5. Add minimal pending path decisions for same-job occurrences across the worker boundary.
6. Publish pending duplicate outcomes only after representative persistence succeeds.
7. Preserve each failed occurrence's error and resolve remaining occurrences without false skips.
8. Add duplicate and RAW duplicate counts to progress, final results, and logs.
9. Validate source stability across hashing, metadata reading, and finalization.

**Success chain:** same full path -> equal size -> equal digest -> persisted representative -> skip.
Different path -> normal import -> checked persistence -> import-time identity stored.
**Failure chain:** source mismatch/read/write error -> failed occurrence -> no new entry and no false
duplicate success; existing entries and their edits remain intact.

**Tests and observable assertions:**

- `RepeatedImportSkipsAndPreservesExistingHistoryRatingAndMasks`: unchanged stored rows remain equal.
- `IdenticalBytesInDifferentFoldersCreateSeparateEntries`: even equal size and SHA never cross paths.
- `SameFilenameWithDifferentFolderAndContentImportsSeparately`: filename collisions remain harmless.
- `SamePathSizeMismatchReportsSourceChangedWithoutDigestComparison`: instrumentation sees no SHA comparison.
- `SamePathEqualSizeHashMismatchReportsSourceChanged`: no placeholder or skip is published.
- `SameJobEquivalentPathSpellingsCreateOneEntry`: delayed workers still produce one representative.
- `FailedRepresentativeCannotProduceSuccessfulFollowerSkips`: the real worker boundary preserves errors.
- `SourceReplacementBetweenHashAndMetadataPublishesNoMismatchedFingerprint`: drive the actual reopen boundary.
- `SkipDisabledCreatesIndependentEntriesAndRecordsTheirSourceIdentity`: histories are separate and digests valid.
- `AllDuplicateJobCompletesWithZeroImportsAndFailures`: all requested occurrences receive skip outcomes.
- `UnrelatedUnindexedOfflineEntryDoesNotBlockNewPathImport`: the earlier global legacy restriction is absent.

**Build/run:** build and run targeted ImportServiceTest, SourceFingerprintTest, and
ImportContentClassificationTest cases. Run conditional ImportPipelineDocumentTest cases when available.
Retain the existing 1,100-hardlink capacity test: different full paths must remain independent even
when skipping is enabled. Add a repeated-same-path capacity case separately where needed.

**Exit criteria:**

- [x] Every comparison-table row has production-path evidence.
- [x] Same-job ordering and failed representative tests pass; the Windows reader prevents replacement at the metadata reopen boundary.
- [x] Import and skip counts satisfy both total and RAW subset accounting.

**Expected diff:** 1,100-1,900 lines.

#### I3 completion record (2026-11-09)

**Status:** complete on Windows. Shared core LOC is reported under I2.

Admission compares full normalized paths before creating a placeholder. A matching stored byte
size is required before hashing. Every recorded digest at that path must agree. Only the required
legacy IDs receive `legacy_current_source`, with an explicit import-log record. Every successful
new entry records import-time identity even when skipping is disabled. Progress, result, log,
RAW subsets, and automation processed counts include confirmed duplicate occurrences.

A pending-path set plus checked batch flush replaces the planned follower-index queue. On reaching
a pending repeat, submission waits for that representative's real outcome before deciding the next
occurrence. This uses the existing single submission owner, preserves order, and needs no added
version/cancellation mechanism. A database failure stops admission; it is not retried through followers.

**Success chain:** normalized same path -> byte size -> SHA-256 -> persisted representative
-> duplicate log and counters; distinct path -> independent Image/root/fingerprint.

**Failure chain:** size/hash mismatch -> SOURCE_CHANGED with no new entry or skip; failed
representative -> actual failure and no follower success; read/comparison error -> explicit failure.

**Executed evidence:** repeated same paths, equivalent spellings, equal bytes in different folders,
same names with different contents, conflicting same-path identities, disabled skipping, all-duplicate
completion, RAW subset accounting, and unavailable unrelated sources. Retained rows, inactive edited
Versions, undone active history, mask assets, ratings, and memberships are compared across reopen.
The existing 1,100-distinct-hard-link capacity test passes with skipping enabled. Windows source
replacement evidence comes from `OpenReadProtectsTheMetadataReopenBoundaryFromReplacement`: a held
production reader prevents delete and write/truncate after hashing. This tests the actual reader
boundary; it does not claim a service-level injected metadata pause or macOS execution.

### I4 - Default Behavior setting and request propagation

**Objective and deliverables:** expose the approved default-on setting and apply the same policy
to file, folder, drop, and automation requests without an extra import dialog.

**Inputs and prerequisites:** I3; Settings' existing pending/reset/Apply functions; module-host
composition; native picker and folder scan start methods; automation parameter validation.

**Modules, files, and APIs:** proposed ImportBehaviorPreferences header/source; ApplicationModuleHost;
`qml/SettingDialog.qml`; `album_backend/import_export.*`; library command and task tracker;
English/Chinese translation resources; UI/app CMake and preference/handler tests.

**Data rules and invariants:** QSettings key `import/skipDuplicates`, default true; invalid stored
values use the documented true default; Apply owns persistence; canceled pending changes are discarded;
each request receives one boolean; explicit automation override affects only that request.

**Implementation steps:**

1. Add the import-specific preference owner and module-host exposure.
2. Add the Import Settings section, pending boolean, reset loading, and Apply persistence.
3. Read the resolved option on the UI owner thread at every import start path.
4. Add optional boolean `library.import.skip_duplicates`; omission inherits the saved setting.
5. Carry duplicate counts through the task tracker, progress rings, completion copy, and translations.
6. Preserve native pickers and existing folder scan/type controls without a new duplicate-review UI.
7. Document and manually verify the changed Settings states in both application themes.

**Success chain:** Settings Apply or explicit command option -> request boolean -> ImportService
-> common results -> UI/automation duplicate counts.
**Failure chain:** invalid request type -> parameter error before admission; Cancel -> saved setting
unchanged; owner failure -> existing import error presentation without a silent policy change.

**Tests and observable assertions:**

- `MissingImportPreferenceDefaultsToTrue`: a clean isolated QSettings store enables skipping.
- `ApplyPersistsPreferenceAndCancelDiscardsPendingChanges`: reopening Settings shows the saved choice.
- `PreferenceChangeDoesNotAlterRunningJob`: change Apply after admission and verify the next job differs.
- `FileFolderAndDropRequestsUseTheSavedPreference`: all adaptors pass the same boolean.
- `AutomationOmittedOptionInheritsPreferenceAndExplicitOptionOverridesIt`: the saved setting remains unchanged.
- `DuplicateCountsReachAutomationCompletion`: task processed counts reach requested without false errors.
- `CanceledNativePickerStartsNoImport`: the existing cancellation behavior remains unchanged.

**Build/run:** build proposed ImportBehaviorPreferencesTest, ImportExportHandlerTest,
AutomationLibraryCommandsTest, alcedo_main, and alcedo_cli. Run discovered owner/handler cases.
Verify keyboard focus, Space, Cancel, Apply, narrow windows, and readable help text manually.

**Exit criteria:**

- [x] Default, persistence, cancel, and per-request override assertions pass.
- [x] Every existing import entry point uses the same setting and result accounting.
- [x] Manual Settings checks pass in both themes without a new import-review step.

**Expected diff:** 850-1,500 lines.

#### I4 completion record (2026-11-09)

**Status:** complete. Preference, adapters, UI, translations, caller tests, registration, and VI
documentation use 606 changed lines across 24 files, excluding this completion record.

`ImportBehaviorPreferences` owns the default-on QSettings value on the GUI thread. ApplicationModuleHost
owns and exposes it. Settings loads a pending scalar and applies through that owner; workers receive
only `ImportOptions::skip_duplicates_`. Automation validates an optional boolean override for file,
nonrecursive folder, and recursive folder requests. ImportExportHandler retains requested repeated
occurrences, snapshots the option once, and publishes separate duplicate/RAW duplicate counts.
Progress rings, task results, completion text, and Nikon reimport progress include duplicates.

**Success chain:** Settings Done or explicit library.import option -> resolved request bool
-> ImportService -> confirmed import/skip results -> task counts, progress, and package save.

**Failure chain:** invalid override -> InvalidParams before admission; Escape dismissal -> saved
preference unchanged; source/storage error -> existing service error presentation.

**Executed evidence:** three isolated preference tests; real Settings QML Apply/dismiss/reopen methods;
handler tests; automation file/folder overrides and package reopen; common file-picker/drop adapter
test changes the preference while a job runs and verifies that only the next job changes policy.
An empty native-picker result starts no job. Native file and folder selectors and the folder scan/type
dialog remain in their existing call chains.

Manual Windows checks used the built application: Welcome -> Settings before entering a project;
default enabled checkbox and help text; Tab focus and Space toggle; Escape discard and reopen;
Done persistence and reopen; both Alcedo and Classic themes; and the 1,214 by 793 window layout.
No clipping was observed in the changed Import section. Original Alcedo theme and enabled preference
were restored. The built test instance was closed. The Welcome Settings text action and popup order
were necessary to make this application-wide setting reachable before entering a project.

### I5 - Persistence, integration, and resource qualification

**Objective and deliverables:** provide final acceptance evidence across project save/reopen,
legacy records, real same-name folders, cancellation, and owner failures.

**Inputs and prerequisites:** I4; registered utility, service, preference, UI adaptor, and automation
tests; existing small RAW/raster fixtures and package compatibility tests.

**Modules, files, and APIs:** existing app/UI/automation test files; proposed compact import duplicate
integration fixture where existing fixtures cannot express the real call chain; CMake registrations;
completion records. Do not add production-only test switches or a parallel importer.

**Data rules and invariants:** preserve existing history and all independent paths after reopen;
no failed import leaves a successful fingerprint/binding; no full project source scan for one path;
bounded source read buffers; no GPU decode or quality-policy change for duplicate identification.

**Implementation steps:**

1. Exercise file, folder, drop adaptor, and automation paths against a small saved project.
2. Reopen that package and verify path, fingerprint provenance, ratings, histories, and counts.
3. Inject hash, metadata, image/library/root, fingerprint, and restoration failures at real owners.
4. Measure source read counts and peak buffer allocation on distinct-size and equal-size cases.
5. Compare the existing import-throughput workload with skipping on and off; record added hashing I/O.
6. Record pass, fail, skip, unavailable platform, and manual evidence separately.

**Success chain:** production entry point -> owner import/skip -> package save -> reopen -> assertions.
**Failure chain:** injected production owner error -> explicit final result -> valid existing project
and observable restoration outcome; never retry with different quality or processing technology.

**Tests and observable assertions:**

- `SavedDuplicateImportReopensWithOneSamePathEntryAndUnchangedHistory`: compare complete retained rows.
- `SavedDifferentFolderCopiesReopenAsIndependentImages`: filenames and equal digests do not merge paths.
- `LegacyCurrentSourceInitializationSurvivesReopenWithoutHistoricalClaim`: provenance remains explicit.
- `StoredImportFingerprintDetectsSameSizeSourceReplacement`: persisted initial bytes remain authoritative.
- `CanceledHashWorkReportsEveryOccurrenceExactlyOnce`: requested accounting remains complete.
- `DistinctPathLookupDoesNotHashUnrelatedProjectSources`: source reader instrumentation sees no unrelated reads.
- `StreamingFingerprintMemoryDoesNotGrowWithSourceFileSize`: buffer allocation stays within its stated bound.

**Build/run:** use section 9 for all feature targets and selected direct-caller cases. Check platform
path behavior on Windows and macOS. Record unavailable macOS execution honestly.

**Exit criteria:**

- [x] All section 8 acceptance rows have evidence or an explicit unavailable-platform record.
- [x] Reopen and complete retained-history comparisons pass through real product owners.
- [x] Resource measurements show bounded buffers and explain the initial hash read cost.

**Expected diff:** 650-1,100 lines.

#### I5 completion record (2026-11-09)

**Status:** complete for Windows qualification. The focused production-owner integration fixture is
533 lines. It uses real ProjectService, ImportService, storage, filesystem, and history owners.
Caller and utility test LOC is counted under I1/I4. New tests have no production-only test switches.

**Success chain:** real service or GUI/automation adapter -> checked import/skip -> project save
-> reopen -> complete retained-row, independent-path, provenance, and count assertions.

**Failure chain:** source/hash cancellation or real owner write error -> explicit final result
-> restored provisional IDs or explicit restoration error -> unchanged accepted entries.

Section 11 gives the commands, final totals, resource measurements, and platform limits. Earlier
failed iterations were resolved before the final run: owner recursive locking, a test-held history
service keeping the database open during reopen, and an old missing-file test expecting no log.
The hard-link test requires native filesystem access outside the sandbox; its workload was retained.

## 8. Cross-phase acceptance matrix

| Behavior | Expected observable result | Main phases |
| --- | --- | --- |
| First import | One entry, valid root, and import-time size/SHA identity | I1-I3 |
| Repeated same path | Successful skip; all existing user state unchanged | I3, I5 |
| Equivalent Windows spelling | Same normalized path and one representative | I1, I3 |
| Same filename in different folders | Separate entries even with equal bytes | I1, I3, I5 |
| Same path changed source | Explicit source-change result; no silent skip or substitute entry | I3, I5 |
| Legacy record | Current-file basis is explicit; historical bytes are not invented | I1, I3, I5 |
| Unrelated missing source | Cannot block a different incoming path | I3, I5 |
| Same-job duplicates | No false success before representative persistence | I2, I3 |
| Hash or owner write failure | Real error; no successful binding/fingerprint for failed work | I2, I3, I5 |
| Cancellation | Completed work persists; all remaining occurrences are counted once | I2, I5 |
| Setting Apply/Cancel | Saved preference changes only on Apply | I4 |
| Setting changed during import | Running request keeps its value; next request reads the new value | I4 |
| Automation override | Omission inherits preference; explicit boolean affects one request | I4 |
| Save and reopen | Fingerprints, provenance, independent paths, and existing histories persist | I1, I5 |
| Existing Undo/Redo and inactive Versions | Import skipping changes no existing history row | I3, I5 |
| Real UI | Existing picker flows and accessible Settings control work in both themes | I4, I5 |

## 9. Build and verification rules

The commands below remain the standard debug instructions. Section 11 records the executed
Windows Release commands and results. Discover test names before selecting cases.

Use PowerShell from the repository root:

```powershell
Set-Location C:\Users\Haidong\Data\Photography\AlcedoStudio
cmd /c scripts\msvc_env.cmd --preset win_debug -DCMAKE_PREFIX_PATH="D:/Qt/6.9.3/msvc2022_64/lib/cmake"
cmd /c scripts\msvc_env.cmd --build --preset win_debug --target SourceImagePathTest SourceFingerprintTest ImportServiceTest ImportContentClassificationTest --parallel 4
$env:PATH = "$PWD/build/debug/vcpkg_installed/x64-windows/debug/bin;$env:PATH"
ctest --test-dir build/debug -N -R 'SourceImagePathTest|SourceFingerprintTest|ImportServiceTest|ImportContentClassificationTest'
ctest --test-dir build/debug --output-on-failure -j 1 -R 'SourceImagePathTest|SourceFingerprintTest|ImportServiceTest|ImportContentClassificationTest'
```

For I4/I5, build ImportBehaviorPreferencesTest, ImportExportHandlerTest, and
AutomationLibraryCommandsTest. Discover current test names and select their affected cases with
`ctest -R`. Build alcedo_main and alcedo_cli for manual and production-command verification.
Check `ALCEDO_CUDA_ENABLED` before naming ImportPipelineDocumentTest. Do not run the full test suite
without the user's explicit request. Do not use unreliable WorkspaceShellTest synthetic-input
delivery as the acceptance basis. Use C++ owner tests and real manual UI verification.

Use only win_debug/win_release or macos_debug/macos_release and their fixed build directories.
Windows configure/build/install uses scripts/msvc_env.cmd. Allow at least 10-20 minutes per build
invocation and keep a healthy process running while it makes progress. Run one build at a time.
Add the Windows vcpkg debug bin path before test execution and run GPU tests serially when needed.
Enable ALCEDO_BUILD_TESTS=ON in the existing macos_debug directory for macOS owner tests.

Put logs and temporary evidence in `build/tmp/import_duplicate_skipping/`. Remove task-specific
temporary files after recording durable findings. Record failed, skipped, and unavailable results
without converting them to passes. New files use LF. Existing CRLF files require the repository's
separate line-ending conversion before content changes. Format changed lines only in existing C++.

## 10. Risks and required responses

- A digest computed later cannot recover overwritten import-time bytes. Capture it during each
  new import and preserve explicit current-source provenance for legacy initialization.
- An old raw-path lookup can miss equivalent spellings. All admission paths must call the shared
  full-path operation before deciding that no binding exists.
- A file can change between hashing and metadata reading. Drive that boundary in tests and reject
  the changed source before publishing mismatched identity.
- A pending first copy can fail. Duplicate counters require persisted representative evidence.
- Current persistence checks are incomplete. I2 must prove failure propagation before I3 ships.
- A shared source-file reference can become offline. Preserve recorded identity and report needed
  same-path read failures; unrelated paths remain independent.
- Stop and revise the phase before its expected diff exceeds 2,000 lines, required owner APIs
  differ from this audit, or implementation would need an unauthorized substitute path.

GitHub issue tracking is coordinated by the parent plan. The implementation session verified
authentication and administrator access on the user's fork, but Issues are disabled there.
Section 11 records this tracking limitation. Upstream was not modified.

## 11. Execution evidence (2026-11-09)

### 11.1 Environment and build results

All product and test builds completed with exit code 0. The existing `build/release` directory
was reused; no debug build existed. Its cached Qt installation is `C:/Qt/6.12.0/msvc2022_64`.
Tests were enabled with `ALCEDO_BUILD_TESTS=ON` through the supported win_release preset.
The cached compiler is MSVC 14.44. Set `VCVARS_USER_VERSION=14.44.35207` so the wrapper selects
its matching headers rather than the machine's newer default 14.51 headers.

Commands ran in PowerShell from the repository root. Configure used:

```powershell
Set-Location C:/Users/Haidong/Data/Photography/AlcedoStudio
$env:VCVARS_USER_VERSION = '14.44.35207'
cmd /c scripts\msvc_env.cmd --preset win_release -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64/lib/cmake" -DALCEDO_BUILD_TESTS=ON
```

Targets successfully built in incremental invocations: `SourceImagePathTest`,
`SourceFingerprintTest`, `ImportDuplicateSkippingTest`, `ImportServiceTest`,
`ImportContentClassificationTest`, `ImportBehaviorPreferencesTest`, `ImportExportHandlerTest`,
`ShortcutSettingsQmlTest`, `AutomationLibraryCommandsTest`, `ProjectPackageBackendTest`,
`alcedo_main`, and `alcedo_cli`. The final focused build commands were:

```powershell
cmd /c scripts\msvc_env.cmd --build --preset win_release --target ImportDuplicateSkippingTest SourceFingerprintTest --parallel 4
cmd /c scripts\msvc_env.cmd --build --preset win_release --target ImportDuplicateSkippingTest --parallel 4
cmd /c scripts\msvc_env.cmd --build --preset win_release --target ProjectPackageBackendTest --parallel 4
```

The application was deployed in its build directory with the matching Qt `windeployqt`, then
launched for the native Settings checks. No installed application files were replaced.

### 11.2 Final targeted results

Final command, exit code 0, 45.81 seconds: **74 selected, 73 passed, 0 failed, 1 skipped**.
CTest names contain both the target prefix and the GoogleTest suite name.

```powershell
$env:PATH = "C:/Qt/6.12.0/msvc2022_64/bin;$PWD/vcpkg/installed/x64-windows/bin;$env:PATH"
$env:QT_QPA_PLATFORM = 'offscreen'
ctest --test-dir build/release --output-on-failure -j 1 -R '^(SourceImagePathTest|SourceFingerprintTest|ImportDuplicateSkippingTest|ImportContentClassificationTest|ImportBehaviorPreferencesTest|ImportExportHandlerTest|AutomationLibraryCommandsTest)\.|^ShortcutSettingsQmlTest\..*ImportPreferenceApplyPersistsAndCancelDiscardsPendingChanges|^ImportServiceTest\..*(InitTest|ImportEmptyTest|MissingFileReportsFailureWithoutCreatingAPlaceholder|ImportToNonExistentDestination)$|^ProjectPackageBackendTest\..*PackageInTheExistingByteLayoutOpensAndWriterMatchesIt$'
```

| Target / selected cases | Passed | Skipped | Assertion coverage |
| --- | ---: | ---: | --- |
| SourceImagePathTest | 3 | 1 | Absolute lexical paths, Windows slash/case equivalence, distinct folders; symlink fixture unavailable |
| SourceFingerprintTest | 6 | 0 | Known digest, all chunks, real missing-file error, cancellation after a read chunk, source handle protection, memory bound |
| ImportDuplicateSkippingTest | 31 | 0 | Comparison table, all-owner completion, validation/rollback, owner failures, partial cancellation, BUSY, reopen, user-state preservation, resources |
| ImportBehaviorPreferencesTest | 3 | 0 | Missing/invalid default, persistent update, notification count |
| ImportServiceTest, selected cases | 4 | 0 | Initialization, empty request, missing-file occurrence log, invalid destination |
| ImportContentClassificationTest | 11 | 0 | Existing RAW/raster content behavior, category exclusions, 1,100 distinct hard-link capacity |
| ImportExportHandlerTest | 2 | 0 | Existing adapter behavior |
| ShortcutSettingsQmlTest, import preference case | 1 | 0 | Real production Settings pending/apply/dismiss/reopen methods and checkbox value |
| AutomationLibraryCommandsTest | 11 | 0 | File/folder policy, request override, invalid type, file/drop common adapter, task counts, package reopen, existing library commands |
| ProjectPackageBackendTest, existing-layout case | 1 | 0 | Existing package byte layout reads and writes identically |

The focused cancellation rerun also passed all three selected tests in 0.79 seconds:

```powershell
ctest --test-dir build/release --output-on-failure -j 1 -R '(PreCanceledJobReportsEveryOccurrenceExactlyOnce|CanceledJobFinalizesCompletedEntriesExactlyOnce|CancellationStopsTheReadWithItsActualError)$'
```

These are subsets of the final result, not additional unique cases. The previous 71-case run
had 70 passes and the same one skip. The final run adds partial cancellation, storage validation,
and package compatibility coverage. No unresolved failure remained in the selected tests.

The plan's descriptive test names map to concrete evidence as follows:

| Planned criterion | Executed evidence |
| --- | --- |
| Full path and platform spelling | EquivalentSeparatorsAndDotSegmentsPreserveTheFullPath; PathCaseFollowsThePlatformRule; SameJobEquivalentPathSpellingsCreateOnePersistedEntry |
| Existing optional schema and explicit legacy basis | LegacyCurrentSourceInitializationSurvivesReopenWithoutHistoricalClaim |
| Complete stored-user-state preservation | DuplicateSkippingPreservesCompleteStoredRowsAcrossReopen; RepeatedImportSkipsAndPreservesExistingRatingAndRoot; DuplicateIntoAnotherAlbumPreservesExistingMemberships |
| Size mismatch avoids hashing | SamePathSizeMismatchDoesNotReadTheChangedSourceContents |
| Equal-size replacement differs from accepted bytes | SamePathEqualSizeHashMismatchReportsSourceChanged; MultipleSamePathEntriesMustAgreeWithEveryRecordedDigest |
| Source reopening stability | OpenReadProtectsTheMetadataReopenBoundaryFromReplacement, at the production reader boundary |
| Persist before success / failed representative | CompletionRunsAfterImageLibraryRootAndFingerprintPersistence; FailedRepresentativeCannotProduceSuccessfulFollowerSkips |
| Explicit storage and restoration errors | ImageWriteFailureCannotBeReportedAsImported; LibraryWriteFailureRestoresProvisionalImages; RootWriteFailureRemovesProvisionalBinding; FingerprintWriteFailureRemovesProvisionalBindingAndRoot; RestorationFailureIsReturnedAndStopsLaterAdmission |
| Running request retains its policy / empty picker | NativePickerCancellationAndFileDropUseTheCommonPreference |
| File/folder request override and package reopen | AutomationOmittedOptionInheritsPreferenceAndExplicitOptionOverridesIt; FolderRequestsKeepThePreferenceAndPerRequestOverride |

### 11.3 Source I/O and memory measurements

Measured cases ran directly with GoogleTest XML output, using the same PATH and serial runtime:

```powershell
& build/release/alcedo_studio/tests/app/ImportDuplicateSkippingTest_runtime/ImportDuplicateSkippingTest.exe --gtest_filter=ImportDuplicateSkippingTest.DistinctPathThroughputWithBothPoliciesPreservesEveryEntry:ImportDuplicateSkippingTest.SamePathSizeMismatchDoesNotReadTheChangedSourceContents --gtest_output=xml:build/tmp/import_duplicate_skipping/io-measurements.xml
& build/release/alcedo_studio/tests/utils/SourceFingerprintTest_runtime/SourceFingerprintTest.exe --gtest_filter=SourceFingerprintTest.StreamingFingerprintMemoryDoesNotGrowWithSourceFileSize --gtest_output=xml:build/tmp/import_duplicate_skipping/memory-measurement.xml
```

All three passed. The local 128-distinct-hard-link workload took **430 ms with skipping enabled**
and **413 ms with skipping disabled**. It asserted 256 independent saved bindings, roots, and
fingerprints across both requests. This is one local measurement, not a general performance target.
Both policies perform the required initial complete hash for each new entry.

For a recorded same-path source enlarged to 32 MiB, the failed comparison recorded **0 bytes**
of additional process ReadTransferCount and returned SOURCE_CHANGED. An unrelated missing legacy
source was not opened during distinct-path admission. Candidate lookup reads minimal project
binding paths once, normalizes them once, and retains only requested matches; it does not read
other source files.

Hashing a 64 MiB fixture recorded **0 bytes above the process's prior peak working set**, with
an asserted increase below 8 MiB. This does not mean zero total memory use. Each reader uses a
fixed 64 KiB buffer; at most 64 admitted entries occupy one batch. On macOS, source validation
also rereads the digest; that platform cost was not measured here.

### 11.4 Review, limits, and tracking

The feature changes before these roadmap records total **2,646 changed lines**: I1 613,
shared I2/I3 894, I4 606, and I5 533. This includes production code, tests, build files,
translations, and the VI update. The prerequisite LF-only commit adds 34 changed lines in
`import_log.hpp`; it is separate from content edits. Each implemented module slice is below
2,000 changed lines. Section 11.5 records per-file size and diff counts.

New data types contain source identity or necessary algorithm outputs. Pending state contains
provisional IDs, paths, a pool pin, and the worker future. It does not duplicate Image metadata
or editing state. Source readers own their native handles until batch publication. New stores
hold Database references, not long-lived connections. No new first-party forward declaration
or OpenCV umbrella include was introduced. Changed and new files use LF. Changed C++ lines and
new C++ files were formatted, and `git diff --check` passed. Repository terminology scans included
the whole roadmap tree and filenames; unrelated existing matches were left outside this scope.

Concrete limitations:

- macOS build, path case behavior, and POSIX source-change validation were not executed on this
  Windows machine. Their source paths exist, but no macOS result is claimed.
- `DifferentUserVisibleSymlinkPathsRemainSeparate` skipped because Windows does not grant symlink
  creation privilege. An attempt outside the sandbox confirmed the same restriction. No system
  security setting was changed. Distinct hard-link paths were exercised successfully.
- Native hard-link fixtures require the test command to run outside the sandbox. Sandbox denial
  was not a product regression and did not justify replacing the 1,100-path workload.
- ImportPipelineDocumentTest was not run: the configured CUDA condition is enabled, but its required
  `sample_images/raw/linear_dng/mfzoty.dng` fixture is absent. Existing RAW/raster classification
  tests and the new real root-persistence assertions passed.
- The full test suite and unreliable WorkspaceShell synthetic-input tests were not run. The
  Settings QML test invokes actual pending/apply/reopen methods; native checks verify input.
- Native picker cancellation was verified through its empty-result adapter. No import or deletion
  was performed in the user's Photos project during manual verification.

GitHub CLI authentication is valid for ClearSights, and `origin` is the user's administrator-owned
fork `ClearSights/AlcedoStudio`. GitHub reports `hasIssuesEnabled=false`. Issue search/creation and
project-status updates therefore cannot complete there. No issue, comment, remote push, or upstream
change was made. In addition to the planned persistence defects, execution observed and fixed the
ImagePoolService recursive lock failure described under I2; it could not receive a tracking issue.
The Welcome Settings access gap found during manual verification was fixed in I4.

Temporary logs and measurement XML were kept under `build/tmp/import_duplicate_skipping/` during
execution. Their durable commands, counts, and measurements are recorded above; task-specific
temporary files are removed at completion. This plan is the retained evidence record.


### 11.5 Changed-file size review

Counts below compare feature content with `6f4857f6` and include untracked new files. Roadmap
record counts are shown separately from the 2,646-line feature total. The LF-only prerequisite
is not counted a second time. Added/deleted counts are line counts, not executable LOC.

The six existing production files above 1,000 lines retain these boundaries:

- `ImportExportHandler` still owns import/export UI lifecycle and presentation. Its import job,
  counters, finalization thread, and signals are the concrete state that an import-task controller
  would need to own in a broader separation. This change removes persistence decisions from that
  adapter and puts them in ImportService. It adds no storage policy to the large adapter.
- `library_commands.cpp` is the library command registry. The added code validates one option and
  routes it to existing application owners. Source comparison does not move into the registry.
- `SettingDialog.qml` composes category pages and owns pending settings values. A separate Default
  Behavior page would need to own pending import/mask controls with explicit apply/reset inputs.
  The 36-line addition uses the existing composition and a separate C++ preference owner; no
  application/import work is placed in QML.
- The UI CMake list and the two translation catalogs are declarative registries. Their two-line
  registration and localized strings do not create new mutable-state owners.

No new production file exceeds 392 lines; the focused integration test is 533 lines. Splitting
methods into additional files without moving their state was not used as a size workaround.

| File (repository relative) | Total lines | Added | Deleted |
| --- | ---: | ---: | ---: |
| `alcedo_studio/src/app/CMakeLists.txt` | 759 | 1 | 1 |
| `alcedo_studio/src/app/image_pool_service.cpp` | 207 | 25 | 0 |
| `alcedo_studio/src/app/import_service.cpp` | 392 | 341 | 417 |
| `alcedo_studio/src/include/app/image_pool_service.hpp` | 153 | 10 | 2 |
| `alcedo_studio/src/include/app/import_service.hpp` | 216 | 31 | 22 |
| `alcedo_studio/src/include/storage/store/image/source_fingerprint_store.hpp` | 44 | 44 | 0 |
| `alcedo_studio/src/include/ui/alcedo_main/album_backend/application_module_host.hpp` | 276 | 6 | 0 |
| `alcedo_studio/src/include/ui/alcedo_main/album_backend/import_behavior_preferences.hpp` | 26 | 26 | 0 |
| `alcedo_studio/src/include/ui/alcedo_main/album_backend/import_export.hpp` | 343 | 17 | 2 |
| `alcedo_studio/src/include/utils/file/source_fingerprint.hpp` | 57 | 57 | 0 |
| `alcedo_studio/src/include/utils/file/source_image_path.hpp` | 14 | 14 | 0 |
| `alcedo_studio/src/include/utils/import/import_error_code.hpp` | 29 | 4 | 0 |
| `alcedo_studio/src/include/utils/import/import_log.hpp` | 125 | 26 | 0 |
| `alcedo_studio/src/storage/CMakeLists.txt` | 65 | 2 | 1 |
| `alcedo_studio/src/storage/store/database.cpp` | 130 | 8 | 0 |
| `alcedo_studio/src/storage/store/image/image_store.cpp` | 207 | 44 | 6 |
| `alcedo_studio/src/storage/store/image/source_fingerprint_store.cpp` | 106 | 106 | 0 |
| `alcedo_studio/src/ui/alcedo_main/CMakeLists.txt` | 1266 | 2 | 0 |
| `alcedo_studio/src/ui/alcedo_main/album_backend/application_module_host.cpp` | 843 | 4 | 0 |
| `alcedo_studio/src/ui/alcedo_main/album_backend/import_behavior_preferences.cpp` | 22 | 22 | 0 |
| `alcedo_studio/src/ui/alcedo_main/album_backend/import_export.cpp` | 1151 | 43 | 51 |
| `alcedo_studio/src/ui/alcedo_main/album_backend/nikon_he_recovery_controller.cpp` | 504 | 4 | 2 |
| `alcedo_studio/src/ui/alcedo_main/application_module_qml_types.cpp` | 74 | 3 | 0 |
| `alcedo_studio/src/ui/alcedo_main/automation/automation_task_tracker.cpp` | 285 | 14 | 10 |
| `alcedo_studio/src/ui/alcedo_main/automation/commands/library_commands.cpp` | 1035 | 27 | 8 |
| `alcedo_studio/src/ui/alcedo_main/i18n/alcedo_main_en.ts` | 6860 | 40 | 0 |
| `alcedo_studio/src/ui/alcedo_main/i18n/alcedo_main_zh_CN.ts` | 9156 | 40 | 0 |
| `alcedo_studio/src/ui/alcedo_main/qml/AppDialogs.qml` | 407 | 3 | 2 |
| `alcedo_studio/src/ui/alcedo_main/qml/ImportProgressOverlay.qml` | 209 | 15 | 3 |
| `alcedo_studio/src/ui/alcedo_main/qml/NikonHeRecoveryDialog.qml` | 508 | 7 | 3 |
| `alcedo_studio/src/ui/alcedo_main/qml/SettingDialog.qml` | 1163 | 36 | 0 |
| `alcedo_studio/src/ui/alcedo_main/qml/ShellSignals.qml` | 120 | 1 | 1 |
| `alcedo_studio/src/ui/alcedo_main/qml/WelcomeDialog.qml` | 685 | 12 | 3 |
| `alcedo_studio/src/utils/CMakeLists.txt` | 78 | 7 | 0 |
| `alcedo_studio/src/utils/file/source_fingerprint.cpp` | 114 | 114 | 0 |
| `alcedo_studio/src/utils/file/source_image_path.cpp` | 34 | 34 | 0 |
| `alcedo_studio/tests/app/CMakeLists.txt` | 981 | 21 | 0 |
| `alcedo_studio/tests/app/import_duplicate_skipping_test.cpp` | 533 | 533 | 0 |
| `alcedo_studio/tests/app/import_service_test.cpp` | 771 | 9 | 5 |
| `alcedo_studio/tests/automation/automation_library_commands_test.cpp` | 290 | 96 | 0 |
| `alcedo_studio/tests/ui/import_behavior_preferences_test.cpp` | 42 | 42 | 0 |
| `alcedo_studio/tests/ui/shortcut_settings_qml_test.cpp` | 930 | 22 | 0 |
| `alcedo_studio/tests/utils/CMakeLists.txt` | 53 | 13 | 0 |
| `alcedo_studio/tests/utils/source_fingerprint_test.cpp` | 119 | 119 | 0 |
| `alcedo_studio/tests/utils/source_image_path_test.cpp` | 44 | 44 | 0 |
| `docs/VI/README.md` | 684 | 16 | 2 |
| `docs/roadmap/alcedo_studio/storage/duplicate_image_cleanup_and_import_plan.md` | 311 | 67 | 20 |
| `docs/roadmap/alcedo_studio/storage/import_duplicate_skipping_plan.md` | 994 | 407 | 56 |
