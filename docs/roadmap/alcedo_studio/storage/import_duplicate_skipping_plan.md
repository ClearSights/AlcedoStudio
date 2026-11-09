# Import Duplicate Skipping Plan

Date: 2026-11-09

Status: planned. The user approved the product decisions. Implementation has not started.

Source audit: `7bcac45d`. Product source was unchanged during this planning task.

Coordination parent: [Duplicate Import and Project Maintenance Plans](duplicate_image_cleanup_and_import_plan.md).

Primary owners: ImportService, Storage, ImportExportHandler, and a proposed
ImportBehaviorPreferences object.

## 1. Approved product decisions

Add duplicate skipping during import as a separate feature. This feature can ship without
Project Maintenance. The user requested planning only in this task. Do not implement these phases
until the user requests implementation.

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

                                         Cancel   Apply
```

Add an Import section to the existing Default Behavior page. Use ThemeCheckBox and the shared
Settings section structure. Use AppTheme tokens and the current Settings typography. Labels wrap
at narrow widths. No new icon, badge, status dot, or palette is required.

Opening Settings loads the saved value into a pending boolean. Space toggles the focused checkbox.
Tab follows the existing dialog order. Apply persists the pending value. Cancel and the existing
dismiss action discard it. Reopening Settings shows the saved value. The preference is application
wide and remains editable when no project is open.

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

A database-only lookup misses A1. Keep minimal current-job pending state keyed by normalized path,
with requested occurrence indices and completion status. Do not retain copied Image state.
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
| I1 | Full-path comparison and source fingerprint storage | utilities, Storage | None | 1,100-1,700 lines | planned |
| I2 | Checked import-owned finalization | ImportService and direct callers | I1 | 1,000-1,700 lines | planned |
| I3 | Size-then-SHA duplicate admission and accounting | ImportService, logs, results | I2 | 1,100-1,900 lines | planned |
| I4 | Default Behavior preference and request propagation | Settings, adaptor, automation | I3 | 850-1,500 lines | planned |
| I5 | End-to-end failure, persistence, and resource evidence | owner/integration tests | I4 | 650-1,100 lines | planned |

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

- [ ] Platform path cases and complete-byte digest assertions pass.
- [ ] Existing project reopen preserves metadata, ratings, history, and format identities.
- [ ] Read/write failure stores no invalid fingerprint.

**Expected diff:** 1,100-1,700 lines. **Completion record:** use section 11; leave it empty until execution.

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

- [ ] Callback-order and write-failure assertions pass through the production owner path.
- [ ] Direct callers need no later UI sync to finish an import.
- [ ] Cancellation and busy behavior remain observable and deterministic.

**Expected diff:** 1,000-1,700 lines. **Completion record:** section 11, empty until execution.

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

- [ ] Every comparison-table row has production-path evidence.
- [ ] Same-job ordering, source replacement, and failed representative tests pass.
- [ ] Import and skip counts satisfy both total and RAW subset accounting.

**Expected diff:** 1,100-1,900 lines. **Completion record:** section 11, empty until execution.

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

- [ ] Default, persistence, cancel, and per-request override assertions pass.
- [ ] Every existing import entry point uses the same setting and result accounting.
- [ ] Manual Settings checks pass in both themes without a new import-review step.

**Expected diff:** 850-1,500 lines. **Completion record:** section 11, empty until execution.

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

- [ ] All section 8 acceptance rows have evidence or an explicit unavailable-platform record.
- [ ] Reopen and complete retained-history comparisons pass through real product owners.
- [ ] Resource measurements show bounded buffers and explain the initial hash read cost.

**Expected diff:** 650-1,100 lines. **Completion record:** section 11, empty until execution.

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

These commands are instructions for future implementation. No configure, build, or test ran during
this planning update. Proposed targets must exist and be discovered before they are named in a run.

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

GitHub issue tracking is coordinated by the parent plan. No issue or follow-up comment is created
by this feature-plan task. Preserve the repository's issue search and maintainer rules when an
authenticated implementation session records the already identified import persistence problems.

## 11. Completion record template

Every phase remains planned until its implementation and evidence exist. Fill a separate record
for each phase during execution:

```text
Phase / date / status:
Source revision and branch:
Actual files, owner APIs, and changed-line count:
Implemented behavior and design changes with reasons:
Primary success call chain:
Primary failure and restore call chain:
Build and targeted test commands with exit codes:
Discovered / passed / failed / skipped counts:
Manual Settings and native import verification:
Source-read and memory measurements:
Unavailable platforms, remaining defects, and issue links:
Evidence path:
```
