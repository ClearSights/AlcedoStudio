# Geometry, Crop, and Rotate Improvements

Date: 2026-11-14

Revised: 2026-11-16

Status: UI layout approved from the standalone QML preview. Product implementation has not started.
The numerical and persistence design below remains an implementation proposal.

Parent: Standalone extension to the editor workspace roadmap.

Original audit: upstream `main`, `e86edddba6f1559a1444222cb09de679d15592a9`.
Revision audit: upstream `main`, `95f0306a`, integrated by merge `03d44361`.
Source inspection is not execution evidence. The earlier local audit used `37c30924`.
PR dependency audit: #943 remains open at `33156bf2e45057c6a1dd3e78ee83568c6c4466fd`.
That PR is not part of this branch. Its reported tests are not local verification.

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

The user approved the preview's panel layout on 2026-11-16.
The approval covers the control groups, menus, shared control styling, and resizable scroll area.
It does not establish image-processing correctness or approve a persisted format.
The [standalone QML preview](../../../../ui_previews/geometry/README.md) remains a UI study.
Its static Thirds overlay, sample values, state selector, and text action symbols are preview aids.
Do not add those aids to the product or treat them as implementation evidence.
Use the product behavior specified below when connecting the approved layout.
Automatic Upright, Guided Upright, and new lens distortion controls are comparison context.
They are outside this requested manual-transform and orientation change.
The existing lens owner keeps optical distortion correction.
Whole-image orientation is included in the user's revised scope.
Reuse PR #943 for crop pointer interaction, handles, cursors, and the automatic rotation grid.
Keep this plan's manual transforms, selectable composition guides, aspect order, and orientation actions.

## 2. Upstream research and related work

The research used the fetched upstream `main`, open issues, open pull requests, and discussions.
It also searched closed issues for earlier crop-overlay and perspective requests.
Search terms included crop, geometry, transform, perspective, overlay, Upright, and keystone.
Search results describe the state on the date above. They are not a permanent backlog inventory.

| Upstream item | Observed scope | Relationship to this proposal |
| --- | --- | --- |
| [QML workspace plan, Phase 5A](qml_editor_rhi_unified_workspace_plan.md) | Crop mask, guides, handles, input mapping, and retained Qt Quick geometry. Its completion record describes the thirds grid. | Extend the existing overlay renderer. Do not create a second viewer. |
| [GPU pipeline removal plan, Section 6.7](../edit/gpu_dag_final_removal_phase_plan.md#67-uncropped-source-frame-for-the-geometry-panel) | Geometry editing requests an uncropped, unrotated source frame. Normal renders apply document crop and rotation. | Retain this behavior for identity orientation and default transforms. Section 5.5 extends it with orientation and a transformed editing view. |
| [Automation plan, AU8a and AU8b](../automation/cli_mcp_automation_plan.md) | The parameter catalog owns crop ranges, options, conversions, and constraints. The QML panel consumes that catalog. | Extend the catalog and command path. Do not duplicate ranges or aspect lists in QML. |
| [Shortcut registry plan](configurable_keyboard_shortcut_registry_plan.md) | Named commands, input scopes, conflict checks, and saved bindings. Text fields retain text input. | Register composition-guide and Swap Aspect commands through this owner. |
| [Comparison plan](editor_adjustment_comparison_plan.md) | Image placement uses resolved render geometry. The current implementation rejects projective placement. | Include comparison placement in the projective mapping audit. |
| [Shared slider PR #923](https://github.com/zidage/AlcedoStudio/pull/923) | `AdjustmentSlider` now wraps `AlcedoSlider`. Models expose hard limits and soft rail limits. `CollapsibleSection` adds its body insets. | Use the current wrappers and model properties. Do not copy the former slider or add section padding twice. |
| [GPU release PR #937](https://github.com/zidage/AlcedoStudio/pull/937) | Export and semantic generation can hold editor GPU resources. Rendering resumes after the last holder releases them. | Retain suspension routing for Geometry editing. Resource suspension is distinct from a transform validation or execution failure. |
| [Crop interaction PR #943](https://github.com/zidage/AlcedoStudio/pull/943), addressing [#942](https://github.com/zidage/AlcedoStudio/issues/942) | Corner rotation zones, edge bands, rotation-drag limits, L brackets, outside-image hover, and an automatic fine rotation grid. | Required upstream input for G0b and later overlay work. Reuse its implementation and tests under Section 2.1. |
| [Open feature #459](https://github.com/zidage/AlcedoStudio/issues/459) | Rotate the entire image and canvas together, without cropping. The issue refers to closed #221. | Include its complete behavior in G0a–G0b and the cross-phase acceptance criteria. The document alone does not resolve the issue. |
| [Open issue #712](https://github.com/zidage/AlcedoStudio/issues/712) | Reports older viewer test registration gaps. The audited source now registers `EditorOverlayInteractionTest` and `OverlayCursorTest`. | Confirm current CMake registration and discovered case names. Do not infer unavailable targets or passing tests from the historical issue. |
| [Open issue #869](https://github.com/zidage/AlcedoStudio/issues/869) | Metal geometry resample and camera-color failures on main. | Record the existing issue when collecting Metal evidence. Do not assume every failure is new. |

No dedicated plan or open feature request for the complete requested guide set and manual
transform controls was found in these searches. No matching open feature discussion was found.
No issue, comment, pull request, or project status was changed during the original research.
The connected account has read access to upstream, without maintainer permissions.
Future contributions must follow `CONTRIBUTING.md`, including its rules for overlapping issues
and human-written follow-up comments.

The 2026-11-16 review recorded four document defects:
[aspect restore #946](https://github.com/zidage/AlcedoStudio/issues/946),
[phase prerequisites #947](https://github.com/zidage/AlcedoStudio/issues/947),
[mapping specification #948](https://github.com/zidage/AlcedoStudio/issues/948), and
[format migration #949](https://github.com/zidage/AlcedoStudio/issues/949).
This revision corrects the plan. It does not claim product fixes or close these reports.
The upstream synchronization also recorded the preview's shared-slider dependency in
[issue #950](https://github.com/zidage/AlcedoStudio/issues/950).

### 2.1 PR #943 dependency and scope boundary

PR #943 is open against `95f0306a` at this review. Do not describe its changes as landed.
G0a can proceed independently. Start G0b and later overlay changes only after #943 is merged
and its merged revision is integrated into the implementation branch.
Recheck its final APIs, tests, and viewport-margin behavior before estimating those phases.
If its final design differs, update this dependency description before implementation.

PR #943 owns these behaviors. Do not implement parallel versions in this plan:

- Crop corner, edge, and rotation-zone hit testing and outside-image hover cursors.
- Press-to-edge offsets, rotation start thresholds, near-center holds, re-anchoring, and zero snapping.
- The per-drag straightening limit, L brackets, edge grips, and angle-badge placement.
- Rotation-active state, the automatic fine grid, its reference lines, and repaint on release.

Reuse `CropGeometry::HitTestWidgetGeometry`, `CropHitTestResult::rotate_hit`,
`CropInteractionController::RotationActive()`, and the existing `crop_rotation_active` projection.
Reuse `CropGeometry::CropAngleBadgeWidgetPoint`, QML `angleBadgeItemPos`, and the
`fine_grid_triangles` drawing output in the existing overlay item.
Use the PR's constants and shared helpers. Do not restore the removed top rotation handle or stem.
Its 45-degree limit applies to one pointer straightening drag. It does not limit the stored angle,
numeric angle entry, discrete quarter turns, or manual perspective fields.

This plan adds composition-guide paths, preference controls, and projective coordinate integration.
Section 5.3 defines their interaction with the existing automatic rotation aid.
Keep PR #943's focused interaction tests. Add only assertions for the new guide and transform inputs.
The PR reports interaction, cursor, and overlay test results; rerun the affected registered cases
after integration before claiming that this branch preserves them.

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
| Aspect orientation | `src/app/editor_parameter_catalog.cpp`, `CropLockedAspectRatio()` and `ConstrainCrop()` | Fixed presets currently select landscape or portrait from source dimensions. Custom uses its stored width and height. Fixed-preset restore must honor the effective ratio introduced by G0a. |
| Guides | `src/ui/editor_rhi/editor_overlay_item.cpp`, `BuildOverlaySceneGeometry()` | The checkout generates dashed thirds without a guide selector. PR #943 adds the automatic fine rotation grid. Add selectable composition paths after that PR; retain its fine-grid output. |
| Interaction | `src/ui/edit_viewer/crop_interaction_controller.cpp`; `src/ui/editor_rhi/editor_interaction_controller.cpp` | The checkout retains the top rotation handle. PR #943 replaces it with corner rotation zones and guarded drag behavior. Reuse that merged owner and extend only the mappings needed for orientation and perspective. |
| Parameters | `src/app/editor_parameter_catalog.cpp` | Owns the `crop_rotate` object, UI defaults, validation, conversion, and crop constraints. Add transform metadata and focused writes. |
| Numeric controls | `src/ui/alcedo_main/qml/AdjustmentSlider.qml`, `AlcedoSlider.qml`; `src/include/ui/alcedo_main/album_backend/editor_adjustment_models.hpp` | The wrapper uses `minimum`/`maximum` and `railMinimum`/`railMaximum`. Rail and value drags use the same owner drag sequence. Add transform metadata without replacing these controls. |
| Document | `src/include/edit/graph/image_geometry_model.hpp` | Owns crop rectangle, rotation, aspect preset, and aspect ratio. `ImageGeometryUpdate` updates selected fields. Extend this owner. |
| Saved format | `src/edit/graph/pipeline_document.cpp`, `ValidateDocumentShape()` and `PipelineDocument::FromJson()`; `src/edit/history/pipeline_document_checkpoint.cpp` | The reader requires the current document format. Root and checkpoint readers also check format fields and canonical JSON. Extend these owners together before writing new geometry. |
| Preferences and keys | `src/ui/alcedo_main/album_backend/editor_behavior_preferences.cpp`; `src/ui/alcedo_main/shortcut_registration.cpp`; `src/include/ui/alcedo_main/shortcut_registry.hpp` | `EditorBehaviorPreferences` uses `QSettings`. Add guide preferences there. Add a Geometry input scope and named commands to the existing shortcut registry. |
| Whole-image orientation | `src/include/edit/graph/image_geometry_model.hpp`; `src/edit/geometry/render_geometry_resolver.cpp` | The document has no discrete rotation or reflection field. The resolver has a `RotatedSourceBounds` output mode, but this is not a persisted orientation command. Add document-owned orientation without changing crop straightening. |
| Mutation | `src/app/editor_parameter_write_parse.cpp`; `src/app/editor_parameter_write.cpp` | `ParseGeometryUpdate()` creates a focused update. The write path calls `document.Geometry().ApplyUpdate()`. Preserve one validated mutation. |
| Geometry | `src/edit/geometry/render_geometry_resolver.cpp`; `src/include/edit/geometry/types.hpp` | Builds crop and rotation mappings. `TransformPoint()` omits homogeneous division. `InvertAffine()` assumes an affine matrix. Add explicit projective operations. |
| GPU sampling | `src/edit/runtime/cuda/geometry_resample.cu`; `src/edit/runtime/metal/shader/geometry_resample.metal`; `src/edit/runtime/opencl/shader/geometry_camera.cl` | Current geometry sampling needs a projective audit. CUDA passes six matrix coefficients. Metal declares six coefficients. Nine stored host coefficients alone do not provide perspective sampling. |
| Comparison | `src/ui/alcedo_main/album_backend/comparison_presentation_image.cpp` | Rejects a projective map. Extend placement and its QML consumer together. |
| Presentation scheduling | `src/ui/alcedo_main/album_backend/editor_session_render_scheduler_port.cpp`; `src/app/editor_session_render_controller.cpp` | The scheduler reroutes frame requests while editor GPU resources are released for export or semantic generation. Use the current `SetSuspendedForGpuRelease` path and holder lifecycle. |
| Tests | `tests/ui/editor_geometry_panel_qml_test.cpp`, `editor_geometry_math_test.cpp`, `editor_crop_interaction_test.cpp`; `tests/app/editor_geometry_overlay_pipeline_test.cpp`; `tests/edit/geometry/` | Existing coverage is for crop and rotation. New perspective assertions must drive production owners and render paths. |

Current registered targets include `EditorGeometryMathTest`, `EditorGeometryPanelQmlTest`,
`EditorCropInteractionTest`, `EditorGeometryOverlayPipelineTest`, and `GpuDagGeometryTest`.
`GpuDagGeometryTest` includes crop-frame, resolver, and document-geometry-use tests.
The current CMake list also registers `EditorOverlayInteractionTest` and `OverlayCursorTest`.
`EditorGeometryOverlayPipelineTest` currently checks CUDA resize behavior.
Its target name alone is not crop-overlay integration evidence. Add the named integration cases below.
This audit reads source. It does not claim that these tests passed during document creation.

The upstream merge did not change `ImageGeometryModel`, the geometry resolver, geometry GPU
sampling files, the Geometry panel, or the published format constants.
The orientation and projective work below is still required.
Upstream also changed RAW import defaults, profile tone-curve import, and Color Grade visibility.
These remain owned by their existing import, document, and graph compiler paths.
Geometry edits and resets must preserve those fields and the active graph's visibility state.

## 5. Proposed product design

### 5.1 Panel structure and controls

Keep the Geometry tab and its current navigation position.
Use the existing adjustment section components and `AppTheme` values.
Follow [DESIGN.md](../../../../alcedo_studio/src/ui/alcedo_main/DESIGN.md).
Use Qt Quick Controls Basic, `CollapsibleSection`, `AdjustmentSlider`, `AdjustmentCombo`,
`ThemeCheckBox`, and `IconActionButton`. Do not introduce a second control style.
`AdjustmentSlider` must retain the shared `AlcedoSlider` implementation.
Pass only the content column's height to `CollapsibleSection.bodyContentHeight`.
The section adds its own `spaceSm` body insets.
Both sections start expanded. Use the existing 320 px preferred panel width and 260–460 px limits.
Reset actions use the existing reset icon, with the distinct accessible names shown below.

```text
Geometry                                      Reset All
  Crop & Rotate                               Reset
    [Rotate Left] [Rotate Right] [Flip H] [Flip V] [Swap Aspect]
    Aspect [preset]
    Lock aspect ratio [checked when the preset is not Free]
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
Use the shared rail drag, numeric value drag, Shift adjustment, and double-click reset behavior.
Use `beginDrag`, `updateDrag`, and `finishDrag` for both drag inputs.
Typed values use `editValue` and `commitImmediately`; rail arrow keys use the model's settled-edit path.
The transform ranges in Section 5.4 are the hard and soft limits alike.
Set `railMinimum = minimum` and `railMaximum = maximum` for these models.
Do not inherit Tone's extra ranges or add Geometry-specific soft limits.
The numeric readout follows the shared control's font, tabular figures, and signed display.
Release of a drag submits one settled history edit through the existing adjustment path.
Reset Transform restores the six manual fields and Constrain Crop to their defaults.
It preserves straightening and discrete orientation. It preserves framing unless the enabled
constraint must adjust it. Publish that adjustment in the same owner operation.
Reset Crop & Rotate requests a full frame, zero straightening, identity orientation, and Free aspect.
It preserves the manual fields and Constrain Crop. If those fields require a constrained frame,
resolve that frame in the same operation. Do not promise a full frame outside valid coverage.
Reset All restores all image-geometry defaults in one owner operation.
Each effective reset creates one history edit. A reset of unchanged values creates none.
Do not implement a group reset by submitting each field's reset separately.
All three resets preserve guide preferences and section expansion state.
Restoring a document, switching images, and opening a panel must not submit an edit.

Disable image-dependent controls until source dimensions and an editable document are available.
Keep the current editor action policy as the authority for edit availability.
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
The label retains the current wording. Its nominal shape is always 3:2.
Landscape uses 3:2 and portrait uses 2:3. It does not detect an arbitrary native source ratio.

Use the existing `aspect_ratio` width and height to store the effective ratio in pre-orientation axes.
The fixed preset identifier names the nominal shape. Its stored ratio selects that shape or its reciprocal.
The catalog must use this stored choice on subsequent edits and restore.
Do not derive it again from source dimensions after Swap Aspect.
When the user first selects a fixed preset, orient its shape to the displayed source dimensions.
For legacy documents, preserve the existing source-based orientation during format conversion.
Do not change the stored crop rectangle during that conversion.
The catalog projects the effective ratio into displayed axes after whole-image orientation.
Custom width and height show that displayed ratio and convert writes back to the stored axes.

The Lock control is a projection of the preset, not a second persisted Boolean.
Free is unlocked. Custom and fixed presets are locked.
Unlocking selects Free and keeps the current frame.
Locking Free selects Custom with the current frame's displayed pixel ratio and keeps that frame.
Selecting a fixed preset or entering Custom dimensions enables the lock.
Each action uses one validated crop-owner update and one settled history edit.

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
Default visibility is Always. Always draws while Geometry editing is active.
During input draws while crop, straightening, or manual-transform input is active.
This includes pointer drags, active numeric editing, and keyboard edits until their settled commit.
Hover and menu opening do not count as image-editing input.
Never draws no composition guide. None also draws no composition guide in every visibility mode.
The crop boundary, handles, and PR #943's automatic rotation aid remain independent of this preference.
Store Triangle orientation as 0 or 1 and Spiral orientation as 0 through 3.
Preserve each asymmetric guide's orientation when the user selects another guide.
The orientation action is disabled for symmetric guides. Triangle has two distinct states.
`O` cycles the offered guide sequence, including None. It does not change the visibility preference.
`Shift+O` changes the current asymmetric guide orientation.
Register these actions with crop-editing scope. Mask mode and editable fields retain their own keys.
Missing settings use the documented defaults. Invalid saved values produce a visible preference error.
Offer Reset Guide beside that error. It restores Thirds, Always, and both orientation defaults.
Do not draw a substitute guide while an invalid preference awaits correction.

#### 5.3.1 Composition guides and the automatic rotation grid

The selectable Grid is a composition guide with seven internal lines on each axis.
PR #943's fine grid is an automatic straightening aid with approximately 28 px cells.
It is not another selector entry. Reuse its existing geometry generator and scene node.
Drive it from `CropInteractionController::RotationActive()` through `crop_rotation_active`.
Do not add a second rotation-active flag, drag controller, or QML grid renderer.
Numeric angle editing and manual-transform input do not activate that pointer-only rotation aid.

Composition-guide visibility applies only to the selected composition guide.
During an active rotation drag, keep the PR's fine grid and stronger thirds reference lines visible
even when the selected guide is None, visibility is Never, or a guide preference is invalid.
When a composition guide is also visible, draw it through the same crop placement and clipping.
If the selected guide is Thirds, draw the coincident thirds lines once with the inherited reference style.
When the rotation drag ends, remove only the automatic aid through the PR's existing repaint path.
The selected composition guide then follows its own visibility preference.
Guide selection, orientation, visibility, and Reset Guide must not change crop input or rotation-active state.

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
Use the following equations in the resolver and in independent numerical test fixtures.
They define Alcedo's proposed mapping. They do not promise Adobe pixel compatibility.

#### 5.4.1 Coordinate and composition rules

Let `W` and `H` be the full reference dimensions after decode orientation.
Let `L = max(W, H)` and `q = L/2`.
Map source pixel-center coordinates to `p = ((x-W/2)/q, (y-H/2)/q)`.
Positive X points right. Positive Y points down.
Keep the current lens-correction order. These equations describe the geometry stage after it.
One offset unit means 0.25 percent of the source long edge.
For Aspect `a`, use `k = 2^(a/200)`, then scale X by `1/k` and Y by `k`.
Use uniform scale `s = scale/100` and translation `d = (0.005*offset_x, -0.005*offset_y)`.

For perspective, let `h = 0.5*horizontal/100`, `v = 0.5*vertical/100`, and `f = 1.5`.
Use a right-handed 3D rotation `Rx(-v) * Ry(h)` on the plane `(p.x, p.y, 0)`.
Project with denominator `f + z`. The equivalent normalized 2D map is:

```text
n = 1 / sqrt(cos(h) * cos(v))
P = [ n*cos(h)             0          0 ]
    [ -n*sin(v)*sin(h)     n*cos(v)   0 ]
    [ -cos(v)*sin(h)/f     -sin(v)/f  1 ]
```

Divide the first two coordinates by the third. The source center maps to zero.
The factor `n` makes the local area scale one at that center.
Positive Vertical widens the bottom. Positive Horizontal widens the right.
Use column vectors. The forward image map before crop is `T(d) * S(s) * A(k) * R(c) * P`.
Here `A(k) = diag(1/k, k, 1)` and `R(c)` applies existing straightening around the crop center `c`.
Express `c` in the same normalized reference plane. Keep that pivot for nondefault perspective too.
This removes an implicit pivot choice and reproduces the existing pivot when `P` is identity.
Crop framing retains the existing normalized rectangle in the reference-sized output plane.
Convert back to reference pixels, apply crop placement, and then apply discrete orientation.
Use the existing extent-rounding rule once per final output dimension.

#### 5.4.2 Displayed controls and stored values

Store manual fields in the pre-orientation reference axes.
Let `O` be the signed 2D axis permutation of the current discrete orientation.
Project `(horizontal, vertical)` through `O` for the displayed perspective controls.
Project `(offset_x, -offset_y)` through `O`, then reverse the displayed Y sign for Y Offset.
Negate Aspect when `O` exchanges the axes. Multiply the displayed straightening angle by `det(O)`.
Scale is unchanged. Convert UI writes through the inverse of these rules.
The table's directional meanings apply to the displayed image.
Build the perspective matrix from stored values in the specified order, not from displayed values.
Do not modify stored transform values or their composition order when orientation changes.
An old document with default transform values must produce its existing pixels and dimensions.
One resolved mapping drives rendering, mask sampling, input mapping, ROI, and export.
Do not add another lens-correction pass.

#### 5.4.3 Crop constraints

With Constrain Crop enabled, intersect valid transformed coverage with the requested frame.
Keep its center when possible. Shrink the frame uniformly to preserve the locked aspect ratio.
In Free mode, preserve the requested frame's current pixel ratio during this automatic shrink.
Translate it only when the requested center cannot produce a valid positive frame.
Choose the nearest feasible center to the requested center in reference pixels.
Choose the largest valid frame at that center. Break equal-distance ties by smaller Y, then smaller X.
Constrain coverage after perspective, straightening, aspect, scale, and offsets, before orientation.
Persist any resulting crop change with the transform change in the same owner operation.
With Constrain Crop disabled, retain the requested frame and show uncovered regions explicitly.
Use the current renderer's opaque black border policy in preview and export.
Do not synthesize missing image content.

### 5.5 Editing view and failure behavior

For default transform values, retain the existing uncropped source editing view without straightening.
Apply the discrete orientation to that editing view. Use its oriented dimensions for viewport fitting.
For nondefault transforms, use a transformed editing view that omits final crop and straightening.
It applies perspective, aspect, scale, and offsets. The overlay represents crop and straightening.
Apply the discrete orientation to the image and overlay together.
Let `E` map source coordinates to the editing image and `F` map them to the final output frame.
Map the final crop boundary and guide paths into the editing image with `E * inverse(F)`.
Use the viewport placement after this map. Map pointer positions back through the corresponding inverse.
Do not assume that an angle and an axis-aligned rectangle can represent the resulting overlay.
Use the same paths for drawing and crop hit testing. Guides themselves do not receive input.
Pass the mapped crop quad to PR #943's hit testing, brackets, grips, and rotation-grid drawing.
Retain its corner-before-edge-before-rotation priority, outside-image cursors, and angle-badge helper.
Extend coordinate conversion for the new transform; do not replace the inherited pointer state machine.
Fit the editing view to the transformed source bounds, with finite extents from the resolver.
This explicitly extends the earlier uncropped-source rule to support the new image operations.
Closing Geometry returns to the normal complete document render without changing the document.

Reject NaN, infinity, out-of-range values, singular matrices, and a projective horizon crossing the
sampled frame. Compute validation and matrix inversion in double precision.
For homogeneous point `u`, reject `abs(w) <= 1e-8 * norm(row3) * norm(u)`, using Euclidean vector norms.
Reject a denominator sign change across the sampled polygon, including inverse mapping of crop corners.
For inversion, reject `abs(det(M)) <= 1e-12 * norm(M)^3`, using the Frobenius matrix norm.
Report the failing condition. Do not clamp a singular matrix into an accepted transform.
Reject an impossible constrained crop before changing the document or history.
Report backend execution failures through the existing render error surface.
Do not present an old image as the successful result of a failed transform.
Do not change backend, decode resolution, or image quality after failure.
An existing `PresentationSuspended` result during export or semantic GPU release is not a failure.
Retain its existing pending-render and resume behavior through the render controller.
After the last GPU holder releases its resources, reroute the current Geometry request on the same backend.
Do not report a suspended frame as a completed transform or replace it with another render path.

### 5.6 Crop shortcuts and whole-image orientation

Use five `IconActionButton` actions below the Crop & Rotate heading.
Give each action an accessible name and a tooltip with its operation.
Use the existing compact hit, optical, and spacing tokens. Keep the order shown below.
At narrow widths, wrap the row into two rows in the same order.
Wrap only when the five compact hit targets do not fit. The standard panel can keep one row.
Keep all five actions available. Do not require the user to open a menu for these actions.
The preview's text symbols are not final icons. Obtain explicit asset approval under `DESIGN.md`
before adding or newly assigning the five SVGs. Reuse the existing reset icon for reset actions.

| Action | Required result |
| --- | --- |
| Rotate Left | Turn the current image and its canvas 90 degrees counterclockwise. Swap width and height. |
| Rotate Right | Turn the current image and its canvas 90 degrees clockwise. Swap width and height. |
| Flip Horizontal | Mirror the image left to right in its current displayed orientation. Keep canvas dimensions. |
| Flip Vertical | Mirror the image top to bottom in its current displayed orientation. Keep canvas dimensions. |
| Swap Aspect | Exchange the crop frame's displayed width and height ratio. Keep image content orientation unchanged. Use scoped `X`. |

The rotate actions satisfy issue #459 without a crop drag or an extra confirmation.
With default manual transforms and no crop, each quarter turn preserves every source corner and edge.
The action must not shrink the crop, zoom to fill the former canvas, or discard image content.
Starting from an existing crop, rotate or reflect the current framed result and its canvas together.
With manual transforms, preserve that accepted result, including any explicit uncovered border.
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
Project crop dimensions and effective aspect ratio into displayed coordinates through the catalog.
Keep continuous straightening in its stored axes; only its UI projection changes under reflection.

Swap Aspect changes framing only. It does not rotate or flip the image.
Preserve the selected preset identifier and exchange its effective displayed ratio.
Write the reciprocal back through the catalog to the existing stored `aspect_ratio` fields.
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
Store manual scalar fields in the table's UI units, including Scale with default 100.
Convert to normalized geometry units only in the resolver.
Reuse `aspect_ratio` for effective crop orientation as defined in Section 5.2.
Do not add a separate crop-lock Boolean or a second aspect-orientation field.
Missing new fields load their defaults. Validate present values and reject invalid types.
Do not silently discard new fields through an older application version.

### 6.1 Saved-format ownership

Use `PipelineDocument`'s existing `format_version` and `kPipelineDocumentFormatVersion`.
The constant is in `src/include/edit/history/pipeline_history_format.hpp` and is 7 at this audit.
G0a defines the next revision for orientation and effective fixed-aspect semantics.
G5 defines the following revision for manual transforms and Constrain Crop.
Each release must reject a later revision that it cannot interpret.
Do not write G5 fields under G0a's revision or add a geometry-specific version scheme.
Advance the existing project metadata version at each step through `pipeline_history_format.hpp`.
Update its supported-version bounds and the project open/save checks together.
A prior application must reject the project before replaying an unsupported geometry batch.
Do not depend only on a document checkpoint to detect an incompatible journal edit.
Keep the packed-project header policy unchanged unless its representation changes.

Extend `ValidateDocumentShape`, `PipelineDocument::FromJson`, and the geometry reader together.
Read supported earlier revisions through explicit version-aware conversion.
This applies to the project versions currently supported at the audit and the new revisions above.
It does not add support for older project versions that the current product already rejects.
Keep their identity-transform rendering and their existing crop pixels unchanged.
Legacy fixed-aspect conversion needs source dimensions and the existing source-orientation rule.
Perform that conversion through the application owner when dimensions are available.
Missing dimensions reject a new orientation or aspect command before mutation.

Include `pipeline_document_checkpoint.cpp`, root-state encoding and decoding, Version state,
history replay, and `app/document_transfer.cpp` in the compatibility audit and implementation.
Validate canonical JSON against its stored revision before conversion.
Never compare an old saved root with a latest-format reserialization as its integrity check.
Retain the original stored root bytes and commit hashes. Do not rewrite historical objects on open.
New roots, checkpoints, and transfer packages must declare their nested document revision accurately.
Apply any live-document conversion with the accepted owner operation, before publication.
Opening an old document must not create an image-history edit.

Verify old-document load, history replay, save/reopen, and transfer at each format step.
Verify rejection with the actual prior-revision reader against a new-format fixture.
Record that reader's source revision. A new-reader rejection test alone does not prove old-reader behavior.

### 6.2 Commands and publication

The catalog exposes a proposed `geometry_transform` field beside the compatible `crop_rotate` field.
Both write to the same document owner. The new field returns only the requested change description.
Expose proposed `geometry.rotate_left`, `geometry.rotate_right`, `geometry.flip_horizontal`,
`geometry.flip_vertical`, and `geometry.swap_aspect` commands through the existing command service.
Use focused owner operations such as `RotateImageQuarterTurn(direction)`, `FlipImage(axis)`,
and `SwapCropAspect()`; their input is the operation, not a copied geometry object.
Each operation returns the existing command success or error result after validation.
The image-geometry owner composes orientation. The crop owner validates Swap Aspect framing.
Here the crop owner means the same `ImageGeometryModel` update path and its geometry validation helper.
It does not mean a new service or a second mutable crop model.
The operations run on the existing editor command executor and publish after the complete change.
Do not copy a document or a complete geometry object to update one value.
QML models remain a read projection. Settings own guide preferences. Resolved geometry owns matrices.
No new state mirror, image buffer copy, executor, or speculative concurrency mechanism is required.
For G2–G4, extend the existing scalar `ImageGeometryParams` render input for resolver tests.
This input is an existing render description. It must not become a second persistent geometry model.
G5 projects the document's accepted fields into that existing input through its current owner.

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
The guide helper generates only selectable composition paths. PR #943 retains automatic grid generation.
Place headers under the matching `src/include/` directories.
Keep new modules small. Include defining headers under the repository's header rules.

## 7. Ordered implementation phases

Before implementation, reread `AGENTS.md`, `CONTRIBUTING.md`, and all applicable skills.
Use `alcedo-qml-ui`, `alcedo-msvc-cmake`, and `opencl-program-registry` where their scopes apply.
All product phases below remain unimplemented. The preview completes none of them.
Estimates include code, tests, build wiring, resources, migration, and documentation.
They exclude PR #943's upstream diff. Reestimate integration against its merged source.

| Phase | Result | Dependency | Expected changed lines | Split reason |
| --- | --- | --- | ---: | --- |
| G0a | Orientation schema, effective aspect semantics, and compatible readers | Audited source | 900-1500 | Separate format and history compatibility from rendering and UI integration. |
| G0b | Complete whole-image rotation, flips, and Swap Aspect actions | G0a and merged PR #943 | 1300-1900 | Deliver orientation actions through the inherited crop interaction and overlay. |
| G1 | Aspect order and complete composition-guide selection | G0b and merged PR #943 | 1000-1600 | Add preference-only composition guides beside the inherited automatic rotation aid. |
| G2 | Projective equations and geometry resolution | G0b | 1200-1800 | Isolate numerical behavior from GPU implementation. |
| G3 | Projective sampling on CUDA, Metal, and OpenCL | G2 | 1000-1800 | Keep backend sampling reviewable without UI changes. |
| G4 | Projective coordinate consumers and editing-view mapping | G0b, G2, G3 | 1200-1900 | Keep masks, ROI, comparison, and export consistent before exposure. |
| G5 | Persisted transforms, catalog, commands, and history | G0a, G0b, G2-G4 | 1100-1900 | Extend the compatible format and its owner operations together. |
| G6 | Transform controls and complete product verification | G0a-G5 | 1000-1800 | Expose controls only after the complete render path exists. |

Reestimate each phase against current source before implementation.
Split any phase whose expected diff can exceed 2000 lines. Do not split one atomic document update.

### G0a. Geometry schema and compatible readers

**Objective and inputs:** Define orientation and effective crop-aspect storage from the audited source.
Do not expose a new image command before G0b completes its rendering path.
**Modules:** `image_geometry_model.hpp`, `pipeline_document.cpp`, `pipeline_history_format.hpp`,
project open/save version checks,
root and checkpoint codecs, history replay, Version state, document transfer, catalog ratio conversion,
and their existing registered tests.
**Rules:** Apply Section 6.1. Use one document-format revision and the existing `aspect_ratio` fields.
Do not mutate old root objects or add a history entry during load.

1. Add the eight-state orientation field and its optional focused update.
2. Define validation for fixed presets' effective ratios and legacy source-based conversion.
3. Add version-aware read and canonical serialization rules before the new format can be written.
4. Extend root, checkpoint, replay, and transfer readers with the same revision rules.
5. Keep the new orientation state inaccessible to user commands until G0b is complete.

**Success chain:** Stored revision -> revision-specific validation -> owner conversion -> valid document.
**Failure chain:** Unsupported revision, invalid orientation, or invalid effective ratio -> explicit load
or command error; no replacement of the active document and no partial publication.
**Tests:** `LegacyGeometryKeepsCropPixelsAndSourceBasedAspect`,
`FixedPresetEffectiveAspectSurvivesDocumentRoundTrip`,
`OldHistoryRootKeepsCanonicalBytesAndCommitHash`, and
`PriorReaderRejectsOrientationDocumentRevision` assert their named behavior.
**Commands:** Confirm and build the existing document, checkpoint, replay, transfer, and
`EditorParameterCatalogTest` targets. Run their focused compatibility cases under Section 8.
**Exit criteria:** Earlier documents retain crop pixels and history identity. The new format round-trips.
The actual prior reader rejects the new revision. No new orientation action is exposed yet.
**Expected diff:** 900-1500 lines.
**Completion record:** Pending. Use Section 9.

### G0b. Whole-image orientation and Crop actions

**Objective and inputs:** Deliver issue #459 and all five actions after G0a and integrated PR #943.
**Modules:** Image geometry model and update, catalog, field parser, command service, resolver,
history, Crop QML, shortcut registry, interaction mapping, comparison, export, and focused tests.
**Rules:** Exact eight-state orientation; complete source preservation without a crop operation;
one owner update per action; identity defaults for older documents.
Retain PR #943's crop interaction, constants, handles, cursors, and automatic rotation grid.

1. Use G0a's orientation field, effective aspect ratio, and validated owner update.
2. Add displayed-coordinate rotation and flip commands. Compose exact orientation states.
3. Resolve oriented extents and pixel-center mapping after existing crop and straightening.
   Include orientation in pixel-changing geometry content keys.
4. Extend image placement, mask mapping, viewer interaction, comparison, and export together.
   Feed oriented crop coordinates to the inherited hit test and drawing helpers.
5. Implement Swap Aspect and Lock through the crop owner. Connect the button and scoped `X`.
6. Add the compact action row, translated names, reset scopes, and load-only restore.
   Obtain approval for final action SVGs as required in Section 5.6.

**Success chain:** Crop action or `X` -> named command -> focused geometry owner operation
-> one history edit -> resolved mapping -> image, overlays, comparison, and export.
**Failure chain:** Missing dimensions, rejected update, or invalid crop -> explicit command error;
no partial geometry change or history entry. GPU failure -> real render error without substitution.
**Tests:** `QuarterTurnSwapsCanvasAndPreservesEverySourcePixel` uses an asymmetric labeled image.
`FourQuarterTurnsAndTwoMatchingFlipsRestoreExactOrientation` checks all eight starting states.
`FlipAfterQuarterTurnUsesDisplayedAxes` checks labeled corners.
`OrientationActionsPreserveExistingCropContentAndStraightening` checks source selection and angle.
`SwapAspectChangesFrameWithoutRotatingImage` checks preset, Custom, Free, and square cases.
`FixedPresetSwapSurvivesFurtherCropEditsAndReopen` checks the persisted effective ratio.
`LockToggleKeepsFrameAndUsesFreeOrCustomPreset` checks the Lock control's owner operation.
`OrientationAndAspectActionsUndoAndRestoreAfterReopen` checks history and persistence.
`OrientationPreviewMasksComparisonAndExportShareMapping` checks production consumers.
**Commands:** Build `GpuDagGeometryTest`, `EditorParameterCatalogTest`,
`EditorGeometryPanelQmlTest`, `EditorCropInteractionTest`, `EditorGeometryOverlayPipelineTest`,
and the affected registered history, comparison, and export targets. Run only their focused cases.
**Exit criteria:** All issue #459 requirements have evidence, including 180-degree rotation,
save/reopen, and export. Confirm all five actions manually without requiring crop-handle input.
Use exact pixel equality for pure orientation and the existing tolerance for preexisting interpolation.
The existing crop interaction and cursor cases from PR #943 still pass after orientation integration.
**Expected diff:** 1300-1900 lines. Reestimate and split before work if the complete path exceeds 2000.
**Completion record:** Pending. Use Section 9. Do not close #459 from this documentation change.

### G1. Aspect order and composition guides

**Objective and inputs:** Deliver aspect order and composition guides on G0b's PR #943-based overlay.
**Modules:** Aspect options, guide-path helper, overlay item, interaction properties, settings,
shortcut registry, Geometry QML, translations, and UI test registration.
**Rules:** Stable preset identifiers; preference-only composition guides; Section 5.3.1's two drawing inputs.
Reuse PR #943's rotation aid and hit priority. Do not recreate its pointer or grid implementation.

1. Reorder the shared option record. Verify catalog and QML consume the same sequence.
2. Generate each guide's paths with dimension-aware geometry and orientation.
3. Connect composition selection and visibility to `BuildOverlaySceneGeometry` through its existing owner.
   Retain `fine_grid_triangles` and the rotation-active projection. Reuse coincident Thirds paths once.
4. Save preferences through `EditorBehaviorPreferences`. Register scoped shortcut commands.
5. Add the invalid-preference error and Reset Guide action from Section 5.3.

**Success chain:** Selector -> guide preference -> interaction presentation -> guide paths -> QSG lines.
The independent rotation-active projection continues to drive the inherited automatic aid.
**Failure chain:** Invalid stored guide preference -> visible preference error and a Reset Guide action;
the user's reset restores Thirds without an image edit. Invalid crop dimensions -> no guide geometry
until dimensions exist. Do not silently replace an invalid selected guide.
**Tests:** `AspectMenuPlacesVistaVisionBetweenCustomAnd235` checks order and stable identifiers.
`CompositionGuidesStayInsideCropAtAllOrientations` checks bounds within `1e-6` in normalized space.
`DiagonalAnglesAndTrianglePerpendicularsMatchCropPixels` checks angles within `1e-4` radians.
`GuidePreferenceChangesDoNotCreateHistoryEntries` checks the real settings and history owners.
`GuideVisibilityFollowsInputWithoutHidingCropHandles` checks all visibility modes and None.
`InvalidGuidePreferenceRequiresExplicitReset` checks the preference error and reset result.
`CompositionPreferencesPreserveActiveRotationAid` checks None, Never, and invalid preferences during a rotate drag.
`ThirdsAndRotationAidShareReferenceStrokes` checks that coincident thirds are drawn once.
`RotationReleaseRestoresSelectedCompositionGuideVisibility` checks the inherited release and repaint path.
**Commands:** Build `EditorGeometryMathTest`, `EditorGeometryPanelQmlTest`, and the proposed
`CropGuidePathsTest`. Include affected `EditorOverlayInteractionTest`, `EditorCropInteractionTest`,
and `OverlayCursorTest` cases. Run only their focused discovered cases as described in Section 8.
**Exit criteria:** All guide equations, preferences, focus rules, and aspect-order assertions pass.
Confirm guide visibility and orientation manually at DPR 1, 1.5, and 2.
Selected Grid retains its fixed divisions. The automatic grid retains PR #943's screen-space density.
**Expected diff:** 1000-1600 lines.
**Completion record:** Pending. Use Section 9.

### G2. Projective equations and resolution

**Objective and inputs:** Implement Section 5.4 math after G0b without exposing new document controls.
**Modules:** Proposed projective helper, `types.hpp`, geometry resolver, crop coverage math,
resolved geometry, and `GpuDagGeometryTest`.
**Rules:** Preserve default-transform output; reject invalid denominators; round output dimensions once.
Keep G0b's orientation mapping as the final image-and-canvas operation.

1. Implement the specified perspective matrix, pivot, composition order, inversion, and Jacobian.
2. Resolve the transformed coverage and the constrained crop through one geometry owner operation.
3. Calculate conservative required source regions and output extents.
4. Retain affine operations only for inputs that are provably affine.

**Success chain:** Valid scalar transform fields -> composed matrix -> resolved mappings and crop.
**Failure chain:** Singular map, horizon crossing, or empty constrained coverage -> typed error;
the caller receives no geometry result.
**Tests:** `DefaultTransformsPreserveExistingCropPixelsAndDimensions` compares existing cases.
`ProjectivePointRoundTripMatchesWithinTolerance` checks error below `1e-4` source pixel.
`PerspectiveSignsAndAspectScaleOffsetsMatchSpecification` checks asymmetric labeled corner fixtures.
`CombinedPerspectiveMatchesSpecifiedRotationOrder` checks both nonzero perspective fields.
`ConstrainedFrameUsesNearestCenterAndPreservesRequestedRatio` checks locked and Free frames.
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
Compose projective geometry with G0b orientation. Test all eight orientation states.
Use PR #943's merged crop controller and overlay as the integration input.

1. Find affine assumptions in the actual callers of resolved geometry.
2. Replace them with projective point mapping and spatial scale evaluation where required.
3. Extend comparison placement to represent the complete mapping.
4. Compare viewport ROI, complete-image rendering, and export at the same source locations.
5. Implement Section 5.5's editing-view request and `E * inverse(F)` overlay placement.
   Keep this request policy available to G6 without exposing unfinished transform controls.
6. Feed the mapped crop quad and inverse pointer positions to the inherited crop helpers.
   Keep hit distances, rotation-drag thresholds, and fine-grid spacing in widget logical pixels.
   Preserve re-anchoring, release repaint, outside-image hover, and angle-badge placement.

**Success chain:** Resolved mapping -> source-space masks and pointer inverse -> aligned visible result.
**Failure chain:** Invalid mapping -> caller reports geometry error; no misleading mask or comparison image.
**Tests:** `MaskRemainsOnSourceFeatureAfterPerspectiveChange` checks analytic and raster masks.
`PointerRoundTripTargetsRenderedSourcePixel` checks source position within `0.25` displayed pixel.
`PerspectiveRoiPixelsMatchCompleteImageWithinTolerance` compares interior float pixels within `2e-4`.
`PerspectiveComparisonAndExportUseResolvedGeometry` checks corners and output dimensions.
`EditingCropBoundaryMapsBackToFinalOutputCorners` checks combined perspective, aspect, and rotation.
`TransformedCropPreservesInheritedHitZonesAndRotationAid` checks the shared controller and overlay
with perspective, aspect changes, and all eight orientations. Reuse the PR's basic drag cases.
**Commands:** Build the affected owner tests and direct caller tests selected from CMake registration.
Include `EditorGeometryOverlayPipelineTest` and `GpuDagCudaDocumentGeometryRequestTest` when available.
Include affected `EditorCropInteractionTest`, `EditorOverlayInteractionTest`, and `OverlayCursorTest` cases.
Run their focused cases serially for GPU work.
**Exit criteria:** Masks, crop input, ROI, comparison, and export align in combined transform cases.
No projective matrix reaches an affine-only operation.
**Expected diff:** 1200-1900 lines.
**Completion record:** Pending. Use Section 9.

### G5. Document, commands, and history

**Objective and inputs:** Persist the complete transform after G2-G4 support exists.
**Modules:** `ImageGeometryModel`, focused update, field parser and writer, catalog,
serialization compatibility owner, graph content keys, history labels, and service tests.
**Rules:** One atomic geometry update; missing fields use defaults; Section 6.1's next format revision.

1. Extend the existing model and update representation with the named fields and compatible readers.
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
Repeat G0a's canonical-history and actual prior-reader checks for this format step.
**Commands:** Build and run `EditorParameterCatalogTest` and affected parser, document, history,
and automation targets after confirming their current registrations.
**Exit criteria:** UI-independent commands persist and replay every field. Invalid writes create no history entry.
**Expected diff:** 1100-1900 lines.
**Completion record:** Pending. Use Section 9.

### G6. Transform UI and product acceptance

**Objective and inputs:** Expose the complete Section 5 design after G1-G5.
**Modules:** Geometry QML, catalog adapter, adjustment submission, editing-view request policy,
interaction presentation, translations, theme documentation if needed, and direct service tests.
**Rules:** Load-only restore; one angle owner; separate crop and content controls; explicit failures.

1. Add catalog-driven controls, section resets, numeric editing, and tooltips.
   Supply the current slider models' hard and soft bounds, and preserve shared section inset ownership.
2. Select G4's transformed editing view without clearing persistent document fields.
3. Connect render errors and accessibility names. Verify focus and narrow-window scrolling.
4. Exercise the complete workflow with real landscape, portrait, and architectural photographs.
   Check selectable guides during the inherited rotation aid; do not add another fine-grid control.

**Success chain:** Transform input -> shared command path -> resolved render -> overlay and control projection.
**Failure chain:** Rejected input -> field error and previous accepted value; failed render -> visible real error.
Undo restores the prior accepted transform through the normal history owner.
**Tests:** `TransformPanelRestoreDoesNotSubmitEdits`, `TransformDragCreatesOneSettledHistoryEntry`,
`ResetTransformPreservesCropAndRotation`, and `LeavingGeometryRendersCompleteDocument`.
Run the settled-drag assertion for both rail drag and numeric value drag through `AlcedoSlider`.
The reset-preservation case uses a frame valid under the reset defaults.
`TransformControlsUseDisplayedAxesAfterOrientation` checks catalog reads and writes in all eight states.
`SectionResetsPreserveGuidesAndCreateOneHistoryEdit` checks owner updates rather than field-reset loops.
`ResetCropWithManualTransformsKeepsConstrainedCoverage` checks the full-frame request and its adjustment.
`GeometryResetPreservesImportLookAndGradeVisibility` checks the existing document fields.
`GeometryPresentationResumesAfterLastGpuReleaseHolder` checks the existing scheduler and render owners.
Use owner/service assertions first. Use focused QML property checks for layout and restore.
**Commands:** Build `alcedo_main`, `EditorGeometryPanelQmlTest`, `EditorCropInteractionTest`,
and `EditorGeometryOverlayPipelineTest`. Run these focused suites and affected direct callers.
**Exit criteria:** Verify guide changes, combined transforms, crop constraints, Undo/Redo, save/reopen,
version switching, comparison, and full-resolution export. Include rotate, flip, and Swap Aspect
with nondefault transforms. Record manual checks separately.
Verify full-frame corner-zone access, inherited L brackets and cursors, and the existing rotation grid.
**Expected diff:** 1000-1800 lines.
**Completion record:** Pending. Use Section 9.

## 8. Verification and engineering limits

This plan and its standalone UI study do not require a product build or test run.
The commands below are implementation instructions, not completed verification.
The standalone preview's earlier startup and manual inspection predate upstream `95f0306a`.
The upstream adaptation was checked by source inspection and diff checks only.
No build, test suite, or new preview run was performed during the commit-and-sync request.
This PR #943 dependency revision also uses source inspection only. It does not merge that PR or run tests.

Use PowerShell from the repository root on Windows.
The example uses the Qt installation verified by the standalone preview.
Use the existing build's configured Qt path when it differs. Do not change Qt versions to run a phase.

```powershell
Set-Location C:\Users\Haidong\Data\Photography\AlcedoStudio
cmd /c scripts\msvc_env.cmd --preset win_debug -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64/lib/cmake"
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
After integrating #943, discover the current `EditorCropInteractionTest`, `EditorOverlayInteractionTest`,
and `OverlayCursorTest` cases. Retain its edge-versus-rotation, center-hold, per-drag-limit,
zero-snap, rotation-grid release, corner-bracket, and outside-image cursor assertions.
Run those focused regressions alongside new composition-guide and transform cases.
Confirm that the inherited crop-mode viewport margin permits full-frame corner-zone access.
If that remains unresolved, track the existing #942 requirement before accepting the dependency.
Put temporary evidence under `build/tmp/geometry_crop_transform/` and remove task output after completion.
Read any CRLF file as bytes before editing. Convert it to LF in the required separate commit.
Do not format untouched lines or rename unrelated roadmap files.

| Acceptance area | Required observable result |
| --- | --- |
| Defaults and compatibility | Existing documents render the same pixels and dimensions. Both format steps preserve old history identity. Prior readers reject unsupported documents. |
| Composition | Every selected guide has correct paths, orientation, clipping, and preference persistence. Guides never enter exports. |
| PR #943 integration | Inherited hit zones, rotation guards, brackets, cursors, angle badge, and automatic grid remain intact. Composition preferences do not suppress the active rotation aid. Coincident Thirds strokes appear once. |
| Aspect selection | The complete sequence matches Section 5.2. Reopen restores both preset identity and effective ratio. Lock and Swap Aspect keep their specified states through further edits. |
| Whole-image orientation / #459 | With default manual transforms and no crop, quarter turns preserve every source pixel and swap dimensions. With an existing crop or transform, they preserve the accepted framed result. Two right turns give 180 degrees. Reopen and export preserve orientation. |
| Crop shorthand | All five actions are visible and accessible. Flips use displayed axes. Swap Aspect and scoped `X` change framing without image rotation. |
| Boundaries and invalid input | Combined limits either produce valid finite geometry or a specific rejection before mutation. |
| Crop constraints | Enabled mode contains no uncovered source area. Disabled mode retains framing and explicit black borders. |
| History and versions | One settled edit records transform and crop together. Undo, Redo, reset, and version selection restore all fields. |
| Section resets | Each effective reset creates one owner update and one history edit. Guides remain unchanged. Crop constraints adjust framing only as specified in Section 5.1. |
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
