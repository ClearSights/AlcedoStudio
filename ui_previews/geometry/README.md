# Geometry QML preview

An isolated UI study for Sections 5.1–5.6 of
[the design proposal](../../docs/roadmap/alcedo_studio/ui/geometry_crop_transform_improvements_plan.md).
It does not implement a roadmap phase or change the application.

From the repository root, run:

```powershell
python scripts/preview_geometry_ui.py --qt-bin C:/Qt/6.12.0/msvc2022_64/bin
```

The launcher also finds `qml` on PATH or reads Qt's location from the existing
release CMake cache. It requires Python 3 and a Qt installation with Qt Quick
Controls and the `qml` viewer. No configure, compiler, Alcedo executable, Python
package installation, network access, or image assets are required.

Use `--theme classic` to inspect the second theme. Use `--check` to check QML
syntax and open the preview briefly. Close the window to exit the launcher.

Inspect the aspect menu, Custom width and height, composition-guide menu,
guide orientation availability, numeric fields, sliders, reset actions,
section folding, and scroll area. Drag the center divider to resize the panel
between its existing 260 and 460 px limits. Use the Preview state selector to
inspect disabled controls, example validation text, and an invalid guide
preference with its Reset Guide action.

The preview uses upstream's `AdjustmentSlider` wrapper and its `AlcedoSlider`
control. The local value models expose hard and soft ranges with the same
bounds. Geometry has no extra range beyond the limits in the design.
`CollapsibleSection` adds its own body insets; the preview passes only the
content column height. The numeric font comes from the existing IBM font asset.

The initial sample shows a cropped 3:2 frame. Crop reset restores Free, an
unlocked ratio, zero crop position, full width and height, and zero rotation.
Transform reset preserves the crop controls and restores its six values and
Constrain Crop. Both resets preserve guide selections and section expansion.
Lock selects Custom from Free; unlocking selects Free. Guide orientations use
separate sample counters for Triangle and Spiral. Reset Guide restores Thirds,
Always, and both orientation defaults. These operations affect only local UI
values. They do not validate or draw a resulting crop.

All changes are local sample UI values. The image area and Thirds overlay are
static layout context. Guide selection does not draw other guides. Orientation
actions only identify the selected action. No crop math, image transforms,
shortcuts, history, settings, document access, GPU code, or export are connected.
The static crop corners use L brackets to reflect PR #943's target appearance.
That PR owns corner rotation input, cursors, and the automatic fine rotation
grid. This preview does not recreate those operations or add a fine-grid control.
The Grid menu entry remains the separate selectable composition guide.
The five new action symbols are temporary text glyphs inside `IconActionButton`.
`PreviewActionGlyph` fits their painted bounds to `iconOpticalSizeCompact`,
so the row uses one visual size and the shared compact hit targets.
The slider is the current app's shared control, including its recessed rail;
the preview does not replace its rail, handle, or theme colors.
They do not propose final SVG assets. Existing reset and information assets are
reused for their existing meanings.

The launcher reads the required immutable visual tokens directly from
`app_theme.cpp` and `app_theme.hpp`, including the current theme colors and
their blend expressions. It stops if an expression is unsupported. It packages
the existing shared QML controls, with embedded resource URLs redirected
to source files and `appTheme` reads bound to a local generated singleton,
in a private directory under `build/tmp/`. These generated
files have one viewer-session lifetime and are removed when the viewer exits.
No values are written back. The production C++ theme and QML files are unchanged.

The preview lives outside `alcedo_main` and is loaded as standalone QML. It is
intentionally absent from the application's CMake QML list.
