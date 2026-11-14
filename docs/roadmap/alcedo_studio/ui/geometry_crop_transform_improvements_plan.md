# Geometry, Crop, and Rotate Improvements

Date: 2026-11-14

Status: Design proposal. No product implementation has started.

Parent: Standalone extension to the editor workspace roadmap.

Source: upstream `main`, `e86edddba6f1559a1444222cb09de679d15592a9`.

Branch: `docs/geometry-crop-transform-improvements`.

## 1. Requested result

Make the Geometry tab useful for composition, whole-image orientation, and manual perspective correction.
Preserve the existing crop and rotation workflow.

- Add Grid, Ratio, Spiral, Diagonal, and Triangle composition guides beside Thirds.
  Adobe calls the two mathematical composition guides **Golden Ratio** and **Golden Spiral**.
  These names describe mathematical composition geometry.
- Add Horizontal, Vertical, Aspect, Scale, X Offset, and Y Offset image transforms.
  Horizontal and Vertical change perspective. They are not image flips or crop coordinates.
- Move `1.5:1 (native or VistaVision)` directly after `Custom` and before `2.35:1 (35mm)`.
- Include [issue #459](https://github.com/zidage/AlcedoStudio/issues/459): rotate the image and
  canvas together without losing content. Preserve the result on reopen and export.
- Add compact Crop actions for Rotate Left, Rotate Right, Flip Horizontal, Flip Vertical,
  and Swap Aspect. Give Swap Aspect the scoped `X` shortcut.
- Use Lightroom, Lightroom Classic, and LightCraft as behavior references.
- Reuse current owners for parameters, document changes, history, rendering, and export.

This document proposes implementation details. It does not claim user approval of those details.
Automatic Upright, Guided Upright, and new lens distortion controls are comparison context.
They are outside this requested manual-transform and orientation change.
The existing lens owner keeps optical distortion correction.
Whole-image orientation is included in the user's revised scope.

## 2. Upstream research and related work

The research used the fetched upstream `main`, open issues, open pull requests, and discussions.
It also searched closed issues for earlier crop-overlay and perspective requests.
Search terms included crop, geometry, transform, perspective, overlay, Upright, and keystone.
Search results describe the state on the date above. They are not a permanent backlog inventory.

| Upstream item | Observed scope | Relationship to this proposal |
| --- | --- | --- |
| [QML workspace plan, Phase 5A](qml_editor_rhi_unified_workspace_plan.md) | Crop mask, guides, handles, input mapping, and retained Qt Quick geometry. Its completion record describes the thirds grid. | Extend the existing overlay renderer. Do not create a second viewer. |
| [GPU pipeline removal plan, Section 6.7](../edit/gpu_dag_final_removal_phase_plan.md#67-uncropped-source-frame-for-the-geometry-panel) | Geometry editing requests an uncropped, unrotated source frame. Normal renders apply document crop and rotation. | Preserve this behavior for existing crop edits. Define an explicit transformed editing view for the new controls. |
| [Automation plan, AU8a and AU8b](../automation/cli_mcp_automation_plan.md) | The parameter catalog owns crop ranges, options, conversions, and constraints. The QML panel consumes that catalog. | Extend the catalog and command path. Do not duplicate ranges or aspect lists in QML. |
| [Shortcut registry plan](configurable_keyboard_shortcut_registry_plan.md) | Named commands, input scopes, conflict checks, and saved bindings. Text fields retain text input. | Register composition-guide and Swap Aspect commands through this owner. |
| [Comparison plan](editor_adjustment_comparison_plan.md) | Image placement uses resolved render geometry. The current implementation rejects projective placement. | Include comparison placement in the projective mapping audit. |
| [Open feature #459](https://github.com/zidage/AlcedoStudio/issues/459) | Rotate the entire image and canvas together, without cropping. The issue refers to closed #221. | Include its complete behavior in G0 and the cross-phase acceptance criteria. The document alone does not resolve the issue. |
| [Open issue #712](https://github.com/zidage/AlcedoStudio/issues/712) | Two older viewer test groups lack targets after the CMake split. | Do not rely on those old target names as executable evidence. |
| [Open issue #869](https://github.com/zidage/AlcedoStudio/issues/869) | Metal geometry resample and camera-color failures on main. | Record the existing issue when collecting Metal evidence. Do not assume every failure is new. |

No dedicated plan or open feature request for the complete requested guide set and manual
transform controls was found in these searches. No matching open feature discussion was found.
No issue, comment, pull request, or project status was changed during this research.
The connected account has read access to upstream, without maintainer permissions.
Future contributions must follow `CONTRIBUTING.md`, including its rules for overlapping issues
and human-written follow-up comments.

## 3. External behavior references

| Product | Verified behavior | Use in Alcedo |
| --- | --- | --- |
| [Lightroom desktop crop and geometry](https://helpx.adobe.com/lightroom/desktop/edit-photos/crop-rotate-geometry.html) | Composition-guide selection, aspect lock, crop orientation, straightening, manual perspective and framing, and crop constraints. | Keep Crop and Transform together under Geometry. Separate crop framing from image distortion. |
| [Lightroom Classic Transform](https://helpx.adobe.com/lightroom-classic/desktop/process-and-develop-photos/guided-upright-perspective-correction.html) | Manual Vertical, Horizontal, Rotate, Aspect, Scale, and two offsets. Constrain Crop removes uncovered edges. Upright is a separate correction method. | Add the six requested manual fields. Keep the existing rotation value as the only straightening angle. |
| [Lightroom Classic shortcuts](https://helpx.adobe.com/lightroom-classic/desktop/introduction-to-lightroom-classic/keyboard-shortcuts.html) | `O` cycles crop guides. `Shift+O` changes guide orientation. `X` swaps crop orientation. | Propose scoped `O`, `Shift+O`, and `X` commands. Do not take keys from text editing or mask editing. |
| [LightCraft Crop actions](https://github.com/storytold/lightcraft/blob/f72ba11a2939653c866c07cf9189845ec31f1a50/crates/ui-egui/src/panels/right/crop.rs) | Crop contains Rotate Left, Rotate Right, Flip H, Flip V, and Swap Aspect (X). Buttons call named image or crop commands. | Add one compact action row in Crop & Rotate. Keep image orientation separate from crop-aspect swapping. |
| [LightCraft composition-guide source](https://github.com/storytold/lightcraft/blob/f72ba11a2939653c866c07cf9189845ec31f1a50/crates/ui-egui/src/panels/crop_overlay.rs) | Thirds, Grid, Ratio, Diagonal, Triangle, Spiral, and None. Paths use crop-relative coordinates. Tests cover shape, bounds, and orientation. | Use the same separation between guide construction and screen placement. Verify geometry directly. |
| [LightCraft manual-transform source](https://github.com/storytold/lightcraft/blob/f72ba11a2939653c866c07cf9189845ec31f1a50/crates/pipeline/src/transform.rs) | Manual controls compose into a projective matrix. Coordinates are centered and normalized to half the long edge. | Use one resolved image mapping. Avoid several independent image resampling operations. |

LightCraft was inspected at `f72ba11a2939653c866c07cf9189845ec31f1a50`.
Its source supports Diagonal, Triangle, and Spiral even though its README summary lists fewer guides.
These references establish behavior. They do not establish Adobe's private transform equations.
The numerical choices below are Alcedo proposals, not claims of Adobe pixel compatibility.
Do not copy LightCraft's backend substitution policy into Alcedo.

## 4. Current source audit

Paths in this section start at `alcedo_studio/`.

| Area | Current owner and file | Verified behavior and required extension |
| --- | --- | --- |
| Panel | `src/ui/alcedo_main/qml/EditorGeometryPanel.qml` | Reads crop ranges and aspect options from the catalog. Loads saved crop fields without submitting an edit. Add Crop actions and a distinct Transform section. |
| Aspect order | `src/ui/alcedo_main/editor_support/modules/geometry.cpp`, `CropAspectPresetOptions()` | Twelve options. The 1.5:1 option is eleventh. Move the option record without changing its identifier or ratio. |
| Guides | `src/ui/editor_rhi/editor_overlay_item.cpp`, `BuildOverlaySceneGeometry()` | Generates dashed thirds at fixed fractions. No guide selector enters this function. Add guide paths and a selected guide input. |
| Interaction | `src/ui/edit_viewer/crop_interaction_controller.cpp`; `src/ui/editor_rhi/editor_interaction_controller.cpp` | Own crop interaction and viewport mapping. Keep guides independent of handle hit testing. Extend coordinate mapping for transformed content. |
| Parameters | `src/app/editor_parameter_catalog.cpp` | Owns the `crop_rotate` object, UI defaults, validation, conversion, and crop constraints. Add transform metadata and focused writes. |
| Document | `src/include/edit/graph/image_geometry_model.hpp` | Owns crop rectangle, rotation, aspect preset, and aspect ratio. `ImageGeometryUpdate` updates selected fields. Extend this owner. |
| Whole-image orientation | `src/include/edit/graph/image_geometry_model.hpp`; `src/edit/geometry/render_geometry_resolver.cpp` | The document has no discrete rotation or reflection field. The resolver has a `RotatedSourceBounds` output mode, but this is not a persisted orientation command. Add document-owned orientation without changing crop straightening. |
| Mutation | `src/app/editor_parameter_write_parse.cpp`; `src/app/editor_parameter_write.cpp` | `ParseGeometryUpdate()` creates a focused update. The write path calls `document.Geometry().ApplyUpdate()`. Preserve one validated mutation. |
| Geometry | `src/edit/geometry/render_geometry_resolver.cpp`; `src/include/edit/geometry/types.hpp` | Builds crop and rotation mappings. `TransformPoint()` omits homogeneous division. `InvertAffine()` assumes an affine matrix. Add explicit projective operations. |
| GPU sampling | `src/edit/runtime/cuda/geometry_resample.cu`; `src/edit/runtime/metal/shader/geometry_resample.metal`; `src/edit/runtime/opencl/shader/geometry_camera.cl` | Current geometry sampling needs a projective audit. CUDA passes six matrix coefficients. Metal declares six coefficients. Nine stored host coefficients alone do not provide perspective sampling. |
| Comparison | `src/ui/alcedo_main/album_backend/comparison_presentation_image.cpp` | Rejects a projective map. Extend placement and its QML consumer together. |
| Tests | `tests/ui/editor_geometry_panel_qml_test.cpp`, `editor_geometry_math_test.cpp`, `editor_crop_interaction_test.cpp`; `tests/app/editor_geometry_overlay_pipeline_test.cpp`; `tests/edit/geometry/` | Existing coverage is for crop and rotation. New perspective assertions must drive production owners and render paths. |

Current registered targets include `EditorGeometryMathTest`, `EditorGeometryPanelQmlTest`,
`EditorCropInteractionTest`, `EditorGeometryOverlayPipelineTest`, and `GpuDagGeometryTest`.
The last target includes crop-frame, resolver, and document-geometry-use tests.
This audit reads source. It does not claim that these tests passed during document creation.

## 5. Proposed product design

### 5.1 Panel structure and controls

Keep the Geometry tab and its current navigation position.
Use the existing adjustment section components and `AppTheme` values.
Follow [DESIGN.md](../../../../alcedo_studio/src/ui/alcedo_main/DESIGN.md).

```text
Geometry                                      Reset All
  Crop & Rotate                               Reset
    [Rotate Left] [Rotate Right] [Flip H] [Flip V] [Swap Aspect]
    Aspect [preset]     Lock
    Custom width / height, when Custom is selected
    Existing crop position, size, and rotation controls
    Guide [Thirds]      Change guide orientation
    Show guide [Always / During input / Never]
  Transform                                   Reset
    Vertical             [slider and numeric value]
    Horizontal           [slider and numeric value]
    Aspect               [slider and numeric value]
    Scale                [slider and numeric value]
    X Offset             [slider and numeric value]
    Y Offset             [slider and numeric value]
    Constrain Crop       [checked]
```

Retain the existing angle control in Crop & Rotate as the only continuous straightening angle.
Store discrete whole-image orientation separately, as specified in Section 5.6.
Use a tooltip to distinguish crop aspect ratio from Transform Aspect.
The first selects the output frame shape. The second stretches image content.
Use a separate tooltip to distinguish crop X/Y from image X/Y Offset.

Transform sliders support numeric entry, keyboard adjustment, and reset to their default.
Release of a drag submits one settled history edit through the existing adjustment path.
Reset Transform preserves crop framing, straightening, and discrete orientation,
except for an explicit crop constraint adjustment.
Reset Crop & Rotate clears crop framing, straightening, and discrete orientation together.
It preserves manual transform values. Reset All clears both sections in one edit.
Restoring a document, switching images, and opening a panel must not submit an edit.

Disable image-dependent controls until source dimensions and an editable document are available.
Keep numeric values visible during a render. Show a real validation or render error near the section.
Use accessible names for every slider, selector, and icon action.
Keep the normal focus order from top to bottom. Scroll the panel in a narrow window.
Do not shrink labels until they become unreadable.

### 5.2 Aspect menu order

The proposed complete order is:

1. Free
2. Custom
3. 1.5:1 (native or VistaVision)
4. 2.35:1 (35mm)
5. 1:1
6. 16:9
7. 1.9:1 (IMAX)
8. 1.85:1 (DCI)
9. 2.2:1 (70mm)
10. 1.43:1 (70mm IMAX)
11. 4:3 (35mm)
12. 2.76:1 (Panavision Ultra)

Preserve `ratio_1_5_1_native_vistavision` and all other saved option identifiers.
Restore selection by identifier, never by the old list index.
The label retains the current wording. The fixed ratio remains 3:2 for every source shape.
It does not become an automatic source-aspect detector.

### 5.3 Composition guides

Offer None, Thirds, Grid, Ratio, Spiral, Diagonal, and Triangle. Default to Thirds.
Use the full mathematical labels from Section 1 in visible UI copy.

| Guide | Required geometry |
| --- | --- |
| Thirds | Two horizontal and two vertical lines at 1/3 and 2/3. |
| Grid | Seven internal horizontal and seven internal vertical lines, at multiples of 1/8. |
| Ratio | Lines at `1/phi^2` and `1/phi`, where `phi = (1 + sqrt(5)) / 2`. |
| Diagonal | Lines at 45 degrees in crop pixel space from each corner to the opposite boundary. A normalized X alone is insufficient on a nonsquare crop. |
| Triangle | A main diagonal and perpendiculars from the other two corners. Change orientation selects the other main diagonal. |
| Spiral | A complete quarter-arc composition guide derived from a phi-proportioned rectangle. Fit it to the crop. Keep the full curve on square, portrait, and landscape crops. Provide four mirrored orientations. |

Generate paths in crop-relative coordinates. Map them through the current crop placement.
Clip paths to the crop boundary. Preserve constant screen-space stroke width at each zoom and DPR.
Use existing overlay styling. Guides must not capture pointer input or change the image pixels.
Keep guide selection, orientation, and visibility in the existing application settings owner.
They are user preferences. Do not add them to image history, document JSON, or export settings.
The orientation action is disabled for symmetric guides. Triangle has two distinct states.
`O` cycles visible guide types. `Shift+O` changes the current asymmetric guide orientation.
Register these actions with crop-editing scope. Mask mode and editable fields retain their own keys.

### 5.4 Manual transforms

Proposed catalog values use UI units below. Persist the corresponding named values in document geometry.

| Field | UI range | Default | Meaning |
| --- | --- | --- | --- |
| Vertical | -100 to +100 | 0 | Negative values widen the top. Positive values widen the bottom. |
| Horizontal | -100 to +100 | 0 | Negative values widen the left. Positive values widen the right. |
| Aspect | -100 to +100 | 0 | Positive values stretch height and compress width. Negative values reverse that change. |
| Scale | 50 to 150 percent | 100 | Uniform scale about the image center. Separate from viewport zoom. |
| X Offset | -100 to +100 | 0 | Positive values move image content right. |
| Y Offset | -100 to +100 | 0 | Positive values move image content up. |
| Constrain Crop | Boolean | true | Keep the output frame inside valid transformed image coverage. |

Use a 0.1 step and one decimal place for numeric fields.
UI limits do not authorize singular mappings. Validate the composed result before mutation.
Use centered coordinates normalized by half the source long edge, after existing lens correction.
One offset unit means 0.25 percent of the source long edge.
For Aspect `a`, use `k = 2^(a/200)`, then scale X by `1/k` and Y by `k`.
For perspective values, use virtual camera rotations with `0.5 radians * value / 100`.
Use focal length 1.5 in these normalized units. Recenter the projective result at the image center.
Normalize its local area scale there. Match the directional behavior specified in this table.

Compose perspective, existing rotation, aspect, scale, offset, and crop framing in that order.
Apply the discrete whole-image orientation to the resulting image and canvas last.
Store manual fields in the pre-orientation reference axes.
Project axis-dependent controls through the orientation's axis mapping and reflection parity.
The table's directional meanings apply to the displayed image.
Convert UI writes back through that mapping in the catalog. Do not reinterpret stored values on rotation.
Reuse the current rotation pivot when transforms are at their defaults.
An old document with default transform values must produce its existing pixels and dimensions.
The implementation must express the pivot change explicitly when composing nondefault perspective.
One resolved mapping drives rendering, mask sampling, input mapping, ROI, and export.
Keep existing optical correction order. Do not add another lens correction pass.

With Constrain Crop enabled, intersect valid transformed coverage with the requested frame.
Keep its center when possible. Shrink the frame uniformly to preserve the locked aspect ratio.
Translate it only when the requested center cannot produce a valid positive frame.
Persist any resulting crop change with the transform change in the same owner operation.
With Constrain Crop disabled, retain the requested frame and show uncovered regions explicitly.
Use the current renderer's opaque black border policy in preview and export.
Do not synthesize missing image content.

### 5.5 Editing view and failure behavior

For default transform values, retain the existing uncropped source editing view without straightening.
Apply the discrete orientation to that editing view. Use its oriented dimensions for viewport fitting.
For nondefault transforms, propose a transformed editing view that omits final crop and straightening.
It applies perspective, aspect, scale, and offsets. The overlay represents crop and straightening.
Apply the discrete orientation to the image and overlay together.
Both the image and overlay use the resolver's mappings, including the inverse at pointer positions.
This explicitly extends the earlier uncropped-source rule to support the new image operations.
Closing Geometry returns to the normal complete document render without changing the document.

Reject NaN, infinity, out-of-range values, singular matrices, and a projective horizon crossing the
sampled frame. Use a scale-normalized denominator check with a documented numerical tolerance.
Reject an impossible constrained crop before changing the document or history.
Report backend execution failures through the existing render error surface.
Do not present an old image as the successful result of a failed transform.
Do not change backend, decode resolution, or image quality after failure.

### 5.6 Crop shortcuts and whole-image orientation

Use five `IconActionButton` actions below the Crop & Rotate heading.
Give each action an accessible name and a tooltip with its operation.
Use the existing compact hit, optical, and spacing tokens. Keep the order shown below.
At narrow widths, wrap the row into two rows in the same order.
Keep all five actions available. Do not require the user to open a menu for these actions.

| Action | Required result |
| --- | --- |
| Rotate Left | Turn the current image and its canvas 90 degrees counterclockwise. Swap width and height. |
| Rotate Right | Turn the current image and its canvas 90 degrees clockwise. Swap width and height. |
| Flip Horizontal | Mirror the image left to right in its current displayed orientation. Keep canvas dimensions. |
| Flip Vertical | Mirror the image top to bottom in its current displayed orientation. Keep canvas dimensions. |
| Swap Aspect | Exchange the crop frame's displayed width and height ratio. Keep image content orientation unchanged. Use scoped `X`. |

The rotate actions satisfy issue #459 without a crop drag or an extra confirmation.
Starting from an uncropped image, each quarter turn preserves every source corner and edge.
The action must not shrink the crop, zoom to fill the former canvas, or discard image content.
Starting from an existing crop, rotate or reflect the current framed result and its canvas together.
Keep the same selected source content. Do not clear the crop or change the straightening angle.
Compose actions in displayed coordinates. Rotate Left and Rotate Right are exact inverses.
Two equal flips and four equal quarter turns return the original orientation exactly.
Two Rotate Right clicks provide the 180-degree operation requested by issue #459.
Flips act on the current view axes after any quarter turn.

Persist one eight-state orientation value in `ImageGeometryModel`.
Use the values `identity`, `rotate_90_cw`, `rotate_180`, `rotate_90_ccw`,
`flip_horizontal`, `flip_vertical`, `transpose`, and `transverse`.
This value represents rotations and reflections of the rendered frame after decode orientation.
Keep camera orientation metadata unchanged. Do not encode these commands as accumulated float angles.
Compose each action with the current orientation and store the resulting exact state.
Resolve its mapping from pixel centers and extents. A quarter turn is a pixel permutation.
Combine that mapping with the existing geometry sample; do not allocate another full-image copy.
Do not change pre-orientation crop coordinates or transform values to execute an orientation action.
Project crop dimensions and aspect orientation into displayed coordinates through the catalog.

Swap Aspect changes framing only. It does not rotate or flip the image.
Preserve the selected preset identifier and toggle its landscape/portrait orientation.
For Custom, exchange the effective width and height. Preserve the frame center when possible.
In Free mode, exchange the current displayed frame dimensions and remain in Free mode.
Apply the existing crop coverage constraint to the new frame through its owner.
A square frame produces no edit. An impossible frame produces a validation error before mutation.
The button and `X` call the same named command. Text fields and other editing tools retain `X`.

Each effective action creates one settled history edit. Undo and Redo restore it exactly.
Disable the actions when no editable image or source dimensions are available.
Do not require active crop handles to execute an image orientation command.
Save, reopen, version switching, comparison, masks, preview, and export must use the same orientation.
A failed owner operation changes neither geometry nor history. A render failure reports its real error.

## 6. Ownership, data flow, and implementation scope

`ImageGeometryModel` remains the persistent image-geometry owner.
Extend `ImageGeometryUpdate` with optional transform fields, discrete orientation, and the crop constraint flag.
Use proposed JSON keys `vertical`, `horizontal`, `aspect`, `scale`, `offset_x`, `offset_y`,
`constrain_crop`, and `orientation` inside existing document geometry.
Missing new fields load their defaults. Validate present values and reject invalid types.
Do not silently discard new fields through an older application version.
Inspect the existing document-format compatibility owner before writing the new fields.
Use its revision check or migration mechanism to reject unsupported readers explicitly.
Do not add a second version scheme solely for this panel.

The catalog exposes a proposed `geometry_transform` field beside the compatible `crop_rotate` field.
Both write to the same document owner. The new field returns only the requested change description.
Expose proposed `geometry.rotate_left`, `geometry.rotate_right`, `geometry.flip_horizontal`,
`geometry.flip_vertical`, and `geometry.swap_aspect` commands through the existing command service.
Use focused owner operations such as `RotateImageQuarterTurn(direction)`, `FlipImage(axis)`,
and `SwapCropAspect()`; their input is the operation, not a copied geometry object.
Each operation returns the existing command success or error result after validation.
The image-geometry owner composes orientation. The crop owner validates Swap Aspect framing.
The operations run on the existing editor command executor and publish after the complete change.
Do not copy a document or a complete geometry object to update one value.
QML models remain a read projection. Settings own guide preferences. Resolved geometry owns matrices.
No new state mirror, image buffer copy, executor, or speculative concurrency mechanism is required.

```text
QML control or automation command
  -> EditorParameterCatalog validation and conversion
  -> ParseEditorParameterWrite / ParseGeometryUpdate
  -> existing editor command and history owner
  -> ImageGeometryModel::ApplyUpdate (one complete validated change)
  -> graph compilation / ResolveRenderGeometry
  -> selected backend sampling -> frame publication
  -> panel projection, mask placement, comparison, and export use the resolved mapping

Invalid input or constraint failure
  -> validation error -> no document mutation -> no new history entry

GPU execution failure after an accepted edit
  -> real render error -> edit remains available for correction or Undo
  -> no successful frame publication -> no backend substitution
```

Existing files in Section 4 change in their owning phase.
Also audit `resolved_render_geometry.hpp`, graph geometry keys, mask sampling, viewport coverage,
`comparison_presentation_image.cpp`, `EditorComparisonCanvas.qml`, and export geometry requests.
Projective mappings invalidate affine-only scale estimates. Evaluate local scale with the projective
Jacobian and derive conservative sampling bounds over the requested region.
Do not pass a projective matrix into an affine-only helper.
Affine view fitting can retain its existing math where the last matrix row remains `[0, 0, 1]`.
New proposed files are `edit/geometry/projective_transform.{hpp,cpp}`,
`ui/edit_viewer/crop_guide_paths.{hpp,cpp}`, and focused tests for these operations.
Place headers under the matching `src/include/` directories.
Keep new modules small. Include defining headers under the repository's header rules.

## 7. Ordered implementation phases

Before implementation, reread `AGENTS.md`, `CONTRIBUTING.md`, and all applicable skills.
Use `alcedo-qml-ui`, `alcedo-msvc-cmake`, and `opencl-program-registry` where their scopes apply.
All phases below remain proposed. Estimates include code, tests, build wiring, and documentation.

| Phase | Result | Dependency | Expected changed lines | Split reason |
| --- | --- | --- | ---: | --- |
| G0 | Complete whole-image rotation, flips, and Swap Aspect actions | Current main | 1300-1900 | Keep exact orientation and its complete product path separate from projective transforms. |
| G1 | Aspect order and complete composition-guide selection | Current main | 1000-1600 | Keep preference-only changes separate from persisted transforms. |
| G2 | Projective equations and geometry resolution | Current main | 1200-1800 | Isolate numerical behavior from GPU implementation. |
| G3 | Projective sampling on CUDA, Metal, and OpenCL | G2 | 1000-1800 | Keep backend sampling reviewable without UI changes. |
| G4 | Projective coordinate consumers | G0, G2, G3 | 1200-1900 | Keep masks, ROI, comparison, and export consistent before exposure. |
| G5 | Persisted transforms, catalog, commands, and history | G2-G4 | 1000-1800 | Change document fields and their owner operations together. |
| G6 | Transform controls and complete product verification | G0-G5 | 1000-1800 | Expose controls only after the complete render path exists. |

Reestimate each phase against current source before implementation.
Split any phase whose expected diff can exceed 2000 lines. Do not split one atomic document update.

### G0. Whole-image orientation and Crop actions

**Objective and inputs:** Deliver issue #459 and all five actions in Section 5.6 from current main.
**Modules:** Image geometry model and update, catalog, field parser, command service, resolver,
history, Crop QML, shortcut registry, interaction mapping, comparison, export, and focused tests.
**Rules:** Exact eight-state orientation; complete source preservation without a crop operation;
one owner update per action; identity defaults for older documents.

1. Add the orientation field and its validated update to the existing document owner.
   Use the document-format compatibility owner for saved fields, as required in Section 6.
2. Add displayed-coordinate rotation and flip commands. Compose exact orientation states.
3. Resolve oriented extents and pixel-center mapping after existing crop and straightening.
   Include orientation in pixel-changing geometry content keys.
4. Extend image placement, mask mapping, viewer interaction, comparison, and export together.
5. Implement Swap Aspect through the crop owner. Connect the button and scoped `X` to that command.
6. Add the compact action row, translated names, and load-only orientation restore.

**Success chain:** Crop action or `X` -> named command -> focused geometry owner operation
-> one history edit -> resolved mapping -> image, overlays, comparison, and export.
**Failure chain:** Missing dimensions, rejected update, or invalid crop -> explicit command error;
no partial geometry change or history entry. GPU failure -> real render error without substitution.
**Tests:** `QuarterTurnSwapsCanvasAndPreservesEverySourcePixel` uses an asymmetric labeled image.
`FourQuarterTurnsAndTwoMatchingFlipsRestoreExactOrientation` checks all eight starting states.
`FlipAfterQuarterTurnUsesDisplayedAxes` checks labeled corners.
`TransformControlsUseDisplayedAxesAfterOrientation` checks direction and catalog read/write conversion.
`OrientationActionsPreserveExistingCropContentAndStraightening` checks source selection and angle.
`SwapAspectChangesFrameWithoutRotatingImage` checks preset, Custom, Free, and square cases.
`OrientationAndAspectActionsUndoAndRestoreAfterReopen` checks history and persistence.
`OrientationPreviewMasksComparisonAndExportShareMapping` checks production consumers.
**Commands:** Build `GpuDagGeometryTest`, `EditorParameterCatalogTest`,
`EditorGeometryPanelQmlTest`, `EditorCropInteractionTest`, `EditorGeometryOverlayPipelineTest`,
and the affected registered history, comparison, and export targets. Run only their focused cases.
**Exit criteria:** All issue #459 requirements have evidence, including 180-degree rotation,
save/reopen, and export. Confirm all five actions manually without requiring crop-handle input.
Use exact pixel equality for pure orientation and the existing tolerance for preexisting interpolation.
**Expected diff:** 1300-1900 lines. Reestimate and split before work if the complete path exceeds 2000.
**Completion record:** Pending. Use Section 9. Do not close #459 from this documentation change.

### G1. Aspect order and composition guides

**Objective and inputs:** Deliver Section 5.2 and 5.3 from current main.
**Modules:** Aspect options, guide-path helper, overlay item, interaction properties, settings,
shortcut registry, Geometry QML, translations, and UI test registration.
**Rules:** Stable preset identifiers; preference-only guides; unchanged crop hit priority.

1. Reorder the shared option record. Verify catalog and QML consume the same sequence.
2. Generate each guide's paths with dimension-aware geometry and orientation.
3. Connect selection and visibility to the overlay through its existing owner.
4. Save preferences through the settings owner. Register scoped shortcut commands.

**Success chain:** Selector -> guide preference -> interaction presentation -> guide paths -> QSG lines.
**Failure chain:** Invalid stored guide preference -> visible preference error and a Reset Guide action;
the user's reset restores Thirds without an image edit. Invalid crop dimensions -> no guide geometry
until dimensions exist. Do not silently replace an invalid selected guide.
**Tests:** `AspectMenuPlacesVistaVisionBetweenCustomAnd235` checks order and stable identifiers.
`CompositionGuidesStayInsideCropAtAllOrientations` checks bounds within `1e-6` in normalized space.
`DiagonalAnglesAndTrianglePerpendicularsMatchCropPixels` checks angles within `1e-4` radians.
`GuidePreferenceChangesDoNotCreateHistoryEntries` checks the real settings and history owners.
**Commands:** Build `EditorGeometryMathTest`, `EditorGeometryPanelQmlTest`, and the proposed
`CropGuidePathsTest`; run only those targets' discovered tests as described in Section 8.
**Exit criteria:** All guide equations, preferences, focus rules, and aspect-order assertions pass.
Confirm guide visibility and orientation manually at DPR 1, 1.5, and 2.
**Expected diff:** 1000-1600 lines.
**Completion record:** Pending. Use Section 9.

### G2. Projective equations and resolution

**Objective and inputs:** Implement Section 5.4 math without exposing new document controls.
**Modules:** Proposed projective helper, `types.hpp`, geometry resolver, crop coverage math,
resolved geometry, and `GpuDagGeometryTest`.
**Rules:** Preserve default-transform output; reject invalid denominators; round output dimensions once.
Keep the orientation mapping from G0 as the final image-and-canvas operation when integrating G4.

1. Implement matrix composition, inversion, point division, and local Jacobian evaluation.
2. Resolve the transformed coverage and the constrained crop through one geometry owner operation.
3. Calculate conservative required source regions and output extents.
4. Retain affine operations only for inputs that are provably affine.

**Success chain:** Valid scalar transform fields -> composed matrix -> resolved mappings and crop.
**Failure chain:** Singular map, horizon crossing, or empty constrained coverage -> typed error;
the caller receives no geometry result.
**Tests:** `DefaultTransformsPreserveExistingCropPixelsAndDimensions` compares existing cases.
`ProjectivePointRoundTripMatchesWithinTolerance` checks error below `1e-4` source pixel.
`PerspectiveSignsAndAspectScaleOffsetsMatchSpecification` checks asymmetric labeled corner fixtures.
`InvalidProjectiveMappingReturnsErrorWithoutGeometry` checks each invalid-input category.
**Commands:** Build and run `GpuDagGeometryTest` only.
**Exit criteria:** Tests cover landscape, portrait, square, odd extents, locked ratios, combined extrema,
and source dimension changes. Default transforms preserve existing resolver results.
**Expected diff:** 1200-1800 lines.
**Completion record:** Pending. Use Section 9.

### G3. GPU projective sampling

**Objective and inputs:** Execute G2 mappings on every supported production backend.
**Modules:** Geometry pass host parameters and shaders for CUDA, Metal, and OpenCL;
their existing runtime program registration and focused backend tests.
**Rules:** One source lookup mapping; identical coordinate convention; unchanged decode and quality policy.

1. Pass all nine coefficients with consistent host and shader layouts.
2. Divide by the homogeneous coordinate at each sample. Apply the specified border behavior.
3. Keep the existing interpolation policy and fuse transforms into the geometry sample.
4. Update installed shader resources if required. Keep OpenCL registration centralized.

**Success chain:** Resolved geometry -> backend parameters -> projective source sample -> output buffer.
**Failure chain:** Resource or dispatch error -> render failure; no alternate backend or partial frame.
**Tests:** `ProjectiveResampleMatchesIndependentPixelOracle` uses coordinate ramps and labeled corners.
Use source-coordinate error below `1e-3` pixel and float ramp error below `2e-4` in valid interiors.
Check border pixels separately, without ignoring uncovered areas.
`BackendFailureReportsErrorWithoutPublishingFrame` injects a production pass failure.
**Commands:** Build the applicable existing `GpuDagCudaDevelopTest`, `GpuDagMetalDevelopTest`,
or `GpuDagOpenClDevelopTest`. Run the new geometry cases with `ctest -R` and `-j 1`.
Confirm target availability and options first.
**Exit criteria:** Evidence exists for CUDA, Metal, and OpenCL. Unavailable platforms remain unverified.
Compare failures with known upstream reports, including #869.
**Expected diff:** 1000-1800 lines.
**Completion record:** Pending. Use Section 9.

### G4. Coordinate consumers

**Objective and inputs:** Use G2-G3 geometry consistently beyond the image sampler.
**Modules:** Mask sampling and overlays, crop pointer mapping, viewport coverage composition,
ROI and sampling footprints, comparison image placement, and export geometry requests.
**Rules:** Persisted masks retain their source coordinates. All consumers use the same resolved mapping.
Compose projective geometry with G0 orientation. Test all eight orientation states.

1. Find affine assumptions in the actual callers of resolved geometry.
2. Replace them with projective point mapping and spatial scale evaluation where required.
3. Extend comparison placement to represent the complete mapping.
4. Compare viewport ROI, complete-image rendering, and export at the same source locations.

**Success chain:** Resolved mapping -> source-space masks and pointer inverse -> aligned visible result.
**Failure chain:** Invalid mapping -> caller reports geometry error; no misleading mask or comparison image.
**Tests:** `MaskRemainsOnSourceFeatureAfterPerspectiveChange` checks analytic and raster masks.
`PointerRoundTripTargetsRenderedSourcePixel` checks source position within `0.25` displayed pixel.
`PerspectiveRoiPixelsMatchCompleteImageWithinTolerance` compares interior float pixels within `2e-4`.
`PerspectiveComparisonAndExportUseResolvedGeometry` checks corners and output dimensions.
**Commands:** Build the affected owner tests and direct caller tests selected from CMake registration.
Include `EditorGeometryOverlayPipelineTest` and `GpuDagCudaDocumentGeometryRequestTest` when available.
Run their focused cases serially for GPU work.
**Exit criteria:** Masks, crop input, ROI, comparison, and export align in combined transform cases.
No projective matrix reaches an affine-only operation.
**Expected diff:** 1200-1900 lines.
**Completion record:** Pending. Use Section 9.

### G5. Document, commands, and history

**Objective and inputs:** Persist the complete transform after G2-G4 support exists.
**Modules:** `ImageGeometryModel`, focused update, field parser and writer, catalog,
serialization compatibility owner, graph content keys, history labels, and service tests.
**Rules:** One atomic geometry update; missing fields use defaults; explicit unsupported-reader handling.

1. Extend the existing model and update representation with the named fields.
2. Connect catalog validation to geometry constraints through the application owner.
3. Apply transform and resulting crop together. Publish notifications after the complete mutation.
4. Include all pixel-changing fields in geometry content keys and history replay.
5. Verify saved history, reopen, version selection, and automation use the same write path.

**Success chain:** Catalog patch -> parser -> owner validation -> complete document update -> history -> render.
**Failure chain:** Invalid field or persistence operation -> explicit error; no partially applied document write.
Use the existing history owner's failure semantics. Do not invent document copies for rollback.
**Tests:** `TransformWritePreservesUnspecifiedGeometryFields`, `TransformAndConstrainedCropUndoTogether`,
`SavedTransformsRestoreAfterReopen`, `OldGeometryLoadsIdentityTransforms`, and
`UnsupportedReaderRejectsTransformDocument` assert their named behavior through production owners.
**Commands:** Build and run `EditorParameterCatalogTest` and affected parser, document, history,
and automation targets after confirming their current registrations.
**Exit criteria:** UI-independent commands persist and replay every field. Invalid writes create no history entry.
**Expected diff:** 1000-1800 lines.
**Completion record:** Pending. Use Section 9.

### G6. Transform UI and product acceptance

**Objective and inputs:** Expose the complete Section 5 design after G1-G5.
**Modules:** Geometry QML, catalog adapter, adjustment submission, editing-view request policy,
interaction presentation, translations, theme documentation if needed, and direct service tests.
**Rules:** Load-only restore; one angle owner; separate crop and content controls; explicit failures.

1. Add catalog-driven controls, section resets, numeric editing, and tooltips.
2. Implement the transformed editing view without clearing persistent document fields.
3. Connect render errors and accessibility names. Verify focus and narrow-window scrolling.
4. Exercise the complete workflow with real landscape, portrait, and architectural photographs.

**Success chain:** Transform input -> shared command path -> resolved render -> overlay and control projection.
**Failure chain:** Rejected input -> field error and previous accepted value; failed render -> visible real error.
Undo restores the prior accepted transform through the normal history owner.
**Tests:** `TransformPanelRestoreDoesNotSubmitEdits`, `TransformDragCreatesOneSettledHistoryEntry`,
`ResetTransformPreservesCropAndRotation`, and `LeavingGeometryRendersCompleteDocument`.
Use owner/service assertions first. Use focused QML property checks for layout and restore.
**Commands:** Build `alcedo_main`, `EditorGeometryPanelQmlTest`, `EditorCropInteractionTest`,
and `EditorGeometryOverlayPipelineTest`. Run these focused suites and affected direct callers.
**Exit criteria:** Verify guide changes, combined transforms, crop constraints, Undo/Redo, save/reopen,
version switching, comparison, and full-resolution export. Include rotate, flip, and Swap Aspect
with nondefault transforms. Record manual checks separately.
**Expected diff:** 1000-1800 lines.
**Completion record:** Pending. Use Section 9.

## 8. Verification and engineering limits

This document-only change does not require a product build or test run.
The commands below are implementation instructions, not completed verification.

Use PowerShell from the repository root on Windows:

```powershell
Set-Location C:\Users\Haidong\Data\Photography\AlcedoStudio
cmd /c scripts\msvc_env.cmd --preset win_debug -DCMAKE_PREFIX_PATH="D:/Qt/6.9.3/msvc2022_64/lib/cmake"
cmd /c scripts\msvc_env.cmd --build --preset win_debug --target EditorGeometryMathTest EditorGeometryPanelQmlTest GpuDagGeometryTest --parallel 4
$env:PATH = "$PWD/build/debug/vcpkg_installed/x64-windows/debug/bin;$env:PATH"
ctest --test-dir build/debug -N -R '^(EditorGeometryMathTest|EditorGeometryPanelQmlTest|GpuDagGeometryTest)\.'
ctest --test-dir build/debug -R '^(EditorGeometryMathTest|EditorGeometryPanelQmlTest|GpuDagGeometryTest)\.' --output-on-failure -j 1
```

Replace the target list and anchored expression with each phase's confirmed targets.
Do not configure a second build tree. Allow at least 20 minutes for a CUDA build invocation.
Poll the same healthy build process. Never build and test concurrently in one build directory.
Run GPU tests serially. Check runtime DLL copies if behavior does not match the rebuilt source.
On macOS, use `macos_debug` with tests enabled in `build/macos-debug` for Metal evidence.
Only the user can request a full test suite. Focused suites do not count as a full-suite result.

Use C++ owner and service tests for input behavior. Do not depend on unreliable offscreen
`WorkspaceShellTest` synthetic input. Record skipped QML coverage honestly.
Put temporary evidence under `build/tmp/geometry_crop_transform/` and remove task output after completion.
Read any CRLF file as bytes before editing. Convert it to LF in the required separate commit.
Do not format untouched lines or rename unrelated roadmap files.

| Acceptance area | Required observable result |
| --- | --- |
| Defaults and compatibility | Existing documents render the same pixels and dimensions. New documents reopen with every transform value. |
| Composition | Every selected guide has correct paths, orientation, clipping, and preference persistence. Guides never enter exports. |
| Aspect selection | The complete sequence matches Section 5.2. Reopen selects by stable identifier. |
| Whole-image orientation / #459 | Left and right quarter turns swap canvas dimensions and preserve every source pixel without crop interaction. Two right turns give 180 degrees. Reopen and export preserve orientation. |
| Crop shorthand | All five actions are visible and accessible. Flips use displayed axes. Swap Aspect and scoped `X` change framing without image rotation. |
| Boundaries and invalid input | Combined limits either produce valid finite geometry or a specific rejection before mutation. |
| Crop constraints | Enabled mode contains no uncovered source area. Disabled mode retains framing and explicit black borders. |
| History and versions | One settled edit records transform and crop together. Undo, Redo, reset, and version selection restore all fields. |
| Rendering and input | CUDA, Metal, OpenCL, masks, pointer mapping, ROI, comparison, and export use the same mapping. |
| Failures | Validation and render errors remain visible. No backend, algorithm, or quality substitution occurs. |
| Accessibility | All new controls have names and keyboard behavior. Text editing and mask shortcuts retain priority. |

Real engineering risks are affine assumptions, projective horizons, crop coverage near extreme
perspective, mask alignment, orientation composition, and old-reader handling.
These are required implementation work.
If an audited owner or call chain has changed, update this proposal before changing code.
If a required compatibility mechanism is absent, specify its exact owner change before persisting fields.
Do not mark a backend complete from source inspection or from another backend's test results.

## 9. Completion evidence template

Complete this record for each implemented phase. Do not fill it during proposal creation.

```text
Phase / date / status:
Source revision and branch:
Actual changed modules and diff size:
Implemented behavior:
Primary success call chain:
Primary failure and restore call chain:
Build and test commands with exit codes:
Discovered / passed / failed / skipped counts:
Manual verification, photographs, zoom, and DPR:
Backend and full-resolution export evidence:
Evidence path and cleanup:
Unimplemented requirements and unavailable platforms:
Related issue numbers:
```
