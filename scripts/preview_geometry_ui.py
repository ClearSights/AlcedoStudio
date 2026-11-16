"""Open the isolated Geometry QML design preview without an Alcedo build."""

import argparse
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "alcedo_studio/src/ui/alcedo_main"
CONFIG = ROOT / "alcedo_studio/src/config"
PREVIEW = ROOT / "ui_previews/geometry"
COMPONENTS = (
    "AdjustmentSlider", "AlcedoSlider", "AdjustmentCombo", "AdjustmentResetButton", "IconActionButton",
    "CollapsibleSection", "EditorInfoHint", "ThemeCheckBox", "DialogActionButton",
)


def theme_properties(theme):
    """Read immutable preview tokens from AppTheme; do not maintain another palette.

    The standalone viewer cannot instantiate the application's C++ AppTheme.
    Resolve only the getter expressions used by these QML files. The generated
    values live in a private launch directory until the viewer exits. They never
    update application settings. An unsupported expression stops the launch.
    """
    source = (UI / "app_theme.cpp").read_text(encoding="utf-8")
    header = (ROOT / "alcedo_studio/src/include/ui/alcedo_main/app_theme.hpp").read_text(
        encoding="utf-8")
    source = re.sub(r"//[^\n]*", "", source)
    theme_name = "MakeClassicTheme" if theme == "classic" else "MakePuerhTheme"
    fields = dict(re.findall(r"\.(\w+)\s*=\s*([^;\n]+?),?\s*$",
                            source.split(f"auto {theme_name}()")[1].split("};")[0], re.M))

    def arguments(text):
        return re.split(r",\s*(?![^()]*\))", text)

    def expression(text):
        text = text.strip().rstrip(",")
        text = text.replace("AppTheme::Instance().", "").replace("theme.", "")
        if re.fullmatch(r"(?:0x[0-9A-Fa-f]+|\d+)", text):
            return int(text, 16 if text.startswith("0x") else 10)
        if re.fullmatch(r"\d+\.\d+", text):
            return float(text)
        field = re.fullmatch(r"GetTheme\(current_theme_index_\)\.(\w+)", text)
        if field:
            return expression(fields[field[1]])
        call = re.fullmatch(r"(\w+)\((.*)\)", text)
        if call:
            name, args = call.groups()
            if not args:
                return getter(name)
            values = [expression(arg) for arg in arguments(args)]
            if name == "QColor":
                return tuple(values) if len(values) == 4 else (*values, 255)
            if name == "Blend":
                a, b, ratio = values
                return tuple(math.floor(x + (y - x) * ratio + 0.5) for x, y in zip(a, b))
            if name == "WithAlpha":
                return (*values[0][:3], values[1])
        raise ValueError(f"Unsupported AppTheme expression: {text}")

    def getter(name):
        definition = re.search(rf"auto\s+(?:AppTheme::)?{name}\(\)\s*(?:const\s*)?"
                               rf"->\s*\w+\s*\{{([^}}]+)\}}", source + header)
        if not definition:
            raise ValueError(f"AppTheme getter not found: {name}")
        return expression(re.findall(r"return\s+(.+?);", definition[1], re.S)[-1])

    qml = "\n".join((UI / "qml" / f"{name}.qml").read_text(encoding="utf-8")
                     for name in COMPONENTS)
    qml += "\n".join(path.read_text(encoding="utf-8") for path in PREVIEW.glob("*.qml"))
    properties = []
    for name in sorted(set(re.findall(r"appTheme\.(\w+)", qml))):
        if name in ("uiFontFamily", "headlineFontFamily", "dataFontFamily"):
            field = {"uiFontFamily": "ui_latin", "headlineFontFamily": "ui_headline",
                     "dataFontFamily": "data"}[name]
            value = re.search(rf'{field}\s*=\s*QStringLiteral\("([^"\n]+)"\)', source)[1]
        elif name == "reduceMotion":
            value = False
        elif name == "motionEasing":
            properties.append(f"    readonly property int {name}: Easing.OutCubic")
            continue
        else:
            value = getter(name)
        if isinstance(value, tuple):
            red, green, blue, alpha = value
            kind, literal = "color", json.dumps(f"#{alpha:02x}{red:02x}{green:02x}{blue:02x}")
        else:
            kind = "bool" if isinstance(value, bool) else "int" if isinstance(value, int) else "string"
            literal = json.dumps(value)
        properties.append(f"    readonly property {kind} {name}: {literal}")
    return "pragma Singleton\nimport QtQuick\nQtObject {\n" + "\n".join(properties) + "\n}\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-bin", type=Path, help="Qt directory containing qml and qmlformat")
    parser.add_argument("--theme", choices=("alcedo", "classic"), default="alcedo")
    parser.add_argument("--check", action="store_true", help="Check QML syntax and open briefly")
    args = parser.parse_args()
    qt_bin = args.qt_bin or (Path(shutil.which("qml") or "").parent if shutil.which("qml") else None)
    if qt_bin is None:
        cache = ROOT / "build/release/CMakeCache.txt"
        match = re.search(r"^Qt6_DIR:PATH=(.+)/lib/cmake/Qt6$",
                          cache.read_text(encoding="utf-8") if cache.exists() else "", re.M)
        if match:
            qt_bin = Path(match[1]) / "bin"
    if qt_bin is None:
        parser.error("Qt viewer not found. Supply --qt-bin <Qt installation>/bin.")
    viewer = qt_bin / ("qml.exe" if os.name == "nt" else "qml")
    if not viewer.is_file():
        parser.error(f"Qt viewer does not exist: {viewer}")
    temporary_root = ROOT / "build/tmp"
    temporary_root.mkdir(parents=True, exist_ok=True)
    # This independent source package is immutable during a viewer session.
    # Map context-property reads to a local singleton and embedded resources to files.
    with tempfile.TemporaryDirectory(prefix="geometry_ui_", dir=temporary_root) as directory:
        stage = Path(directory)
        controls = stage / "controls"
        controls.mkdir()
        for name in COMPONENTS:
            text = (UI / "qml" / f"{name}.qml").read_text(encoding="utf-8")
            text = text.replace("qrc:/panel_icons/", CONFIG.as_uri() + "/panel_icons/")
            text = text.replace("appTheme.", "PreviewTheme.")
            (controls / f"{name}.qml").write_text(text, encoding="utf-8", newline="\n")
        for path in PREVIEW.glob("*.qml"):
            text = path.read_text(encoding="utf-8").replace("PREVIEW_CONFIG_URL", CONFIG.as_uri())
            text = text.replace("appTheme.", "UI.PreviewTheme.")
            (stage / path.name).write_text(text, encoding="utf-8", newline="\n")
        (controls / "PreviewTheme.qml").write_text(theme_properties(args.theme), encoding="utf-8", newline="\n")
        module = "singleton PreviewTheme 1.0 PreviewTheme.qml\n"
        module += "".join(f"{name} 1.0 {name}.qml\n" for name in COMPONENTS)
        (controls / "qmldir").write_text(module, encoding="utf-8", newline="\n")
        if args.check:
            formatter = qt_bin / ("qmlformat.exe" if os.name == "nt" else "qmlformat")
            for path in stage.glob("*.qml"):
                syntax = subprocess.run([str(formatter), str(path)], capture_output=True, text=True)
                if syntax.returncode:
                    print(syntax.stderr)
                    raise SystemExit(syntax.returncode)
        environment = os.environ.copy()
        environment["QT_QUICK_CONTROLS_STYLE"] = "Basic"
        environment["QT_FORCE_STDERR_LOGGING"] = "1"
        command = [str(viewer), "-f", str(stage / "Main.qml")]
        if args.check:
            command += ["--quit-after", "1800"]
        print(f"Opening Geometry UI preview with {viewer}", flush=True)
        result = subprocess.run(command, env=environment, capture_output=True, text=True)
        print(result.stdout, end="")
        print(result.stderr, end="")
        if result.returncode or re.search(r"(?:ReferenceError|TypeError|Error:|Unable to|Cannot assign)",
                                         result.stdout + result.stderr):
            raise SystemExit(result.returncode or 1)


if __name__ == "__main__":
    main()
