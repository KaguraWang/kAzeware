#!/usr/bin/env python3
"""Audit kAzeware against the verified reference snapshot and installed CS2.

Exit codes:
  0 - snapshot fingerprints, required values and critical byte patterns match
  1 - one or more required checks failed
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OFFSETS_H = ROOT / "kAzeware/source/kAzeware/offsets/offsets.h"
DUMP_DIR = ROOT / "dumper/output"
GAME_BIN = Path(
    r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game"
)
CLIENT = GAME_BIN / r"csgo\bin\win64\client.dll"
ENGINE = GAME_BIN / r"bin\win64\engine2.dll"
PANORAMA = GAME_BIN / r"bin\win64\panorama.dll"
SCENE = GAME_BIN / r"bin\win64\scenesystem.dll"

PATTERNS = {
    "GetInaccuracy": (CLIENT, "48 89 5C 24 10 55 56 57 48 81 EC B0 00 00 00 44", True),
    "GetSpread": (CLIENT, "48 63 91 ?? ?? ?? ?? 48 8B 81 ?? ?? ?? ?? 85 D2 78 10 48 83 FA 02 73 0A F3 0F 10 84 90 50 07 00 00 C3", True),
    "GetRecovery": (CLIENT, "48 89 5C 24 ?? 57 48 83 EC 30 48 8B D9 E8 ?? ?? ?? ?? 48 8B F8 48 85 C0 75", True),
    "ComputeSeed": (CLIENT, "48 89 5C 24 08 57 48 81 EC F0 00 00 00 F3 0F 10 0A 48 8D 8C 24 10 01 00 00 41 8B D8 48 8B FA E8", True),
    "FillFire": (CLIENT, "48 8B C4 55 53 56 57 41 55 48 8D 68 98 48 81 EC 40 01 00 00", True),
    "CalcBones": (CLIENT, "48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 B8 98 42", True),
    "ProcessMovement": (CLIENT, "48 8B C4 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC B8 00 00 00 8B AA 94 00 00 00 4C 8B EA", True),
    "ModernJumpCheck": (CLIENT, "48 89 5C 24 08 48 89 6C 24 10 57 48 83 EC 30 48 8B EA 48 8B D9 48 8B 49 08 BA 02 00 00 00", True),
    "CheckJumpButton": (CLIENT, "4C 89 44 24 18 55 56 41 56 48 8D AC 24 70 ED FF FF B8 90 13 00 00", True),
    "VelocityCommand": (CLIENT, "48 8B 54 24 30 48 8B 4C 24 78 E8 ?? ?? ?? ?? 49 8B 07 49 8B CF 48 8B 54 24 30 FF 50 30", True),
    "MovementTraceHull": (CLIENT, "48 89 74 24 ?? 55 57 41 54 41 55 41 56 48 8D AC 24 ?? ?? ?? ?? 48 81 EC A0 01 00 00", True),
    "MovementFilter": (CLIENT, "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 0F B6 41 ?? 33 FF C7 41", True),
    "GrenadeTraceShape": (CLIENT, "48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 41 56 41 57 48 8D AC 24 F0 DB FF FF B8 10 25 00 00", True),
    "AnimatablePrimitives": (SCENE, "48 8B C4 48 89 58 20 4C 89 40 18 48 89 50 10 48 89 48 08 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 D8 FE FF FF 48 81 EC F0 01 00 00", True),
    "CreateMove": (CLIENT, "85 D2 0F 85 ?? ?? ?? ?? 48 8B C4 44 88 40", True),
    "FrameStageNotify": (CLIENT, "48 89 5C 24 ?? 48 89 6C 24 ?? 57 48 83 EC ?? 48 8B F9 33 ED 48 8B 89 ?? ?? ?? ?? 8B DA", True),
    "SetViewAngle": (CLIENT, "85 D2 75 3D 48 63 81", False),
    "GetCmdBySeq": (CLIENT, "40 53 48 83 EC 20 8B DA E8 ?? ?? ?? ?? 4C 8B C0", True),
    "GetCmdArray": (CLIENT, "48 89 4C 24 08 41 56 41 57 48 83 EC 48 4C 63 FA", True),
    "GetEntityCmdSlot": (CLIENT, "48 83 EC 08 4C 8B 0D ?? ?? ?? ?? 4C 8B DA 48 8B", True),
    "ForceButtonsDown": (CLIENT, "48 89 5C 24 18 48 89 6C 24 20 57 48 81 EC 30 02 00 00 48 83 79 38 00", True),
    "MouseInputEnabled": (CLIENT, "40 53 48 83 EC 20 80 B9 BC 0B 00 00 00 48 8B D9 0F 85", True),
    "GetScreenAspectRatio": (ENGINE, "48 83 EC 28 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 0F 57 C9 0F 2F C1 76 10 48 8D 0D", True),
    "DrawGlow": (CLIENT, "40 53 48 83 EC 20 48 8B 54 24 28 48 8D 59 30 48 8B CB E8 ?? ?? ?? ?? 8B 03", True),
    "GetGlowColor": (CLIENT, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F2 48 8B F9 48 8B 54 24 28 48 83 C1 40", True),
    "OnGlowTypeChanged": (CLIENT, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 48 8B D9 F3 0F 10 41 4C F3 0F 10 48 30", True),
    "OverrideView": (CLIENT, "40 53 57 48 83 EC 58 48 8B FA E8 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 80 78 58 00 0F 84", True),
    "FireEventClientSide": (CLIENT, "40 53 56 41 54 48 83 EC 30 48 8B F2 48 8D 99 E0 00 00 00 4C 8B E1", True),
    "DrawLegs": (CLIENT, "4C 8B DC 55 53 56 57 41 56 49 8D AB 78 FB FF FF 48 81 EC 60 05 00 00 8B 42 24", True),
    "DrawSmokeArray": (CLIENT, "48 89 54 24 10 55 57 41 54 48 8D AC 24 40 F9 FF FF 48 81 EC C0 07 00 00 48 8B 05 ?? ?? ?? ?? 4C 8B E2", True),
    "RenderDecals": (CLIENT, "44 88 4C 24 20 55 53 41 54 41 55 48 8D 6C 24 C1 48 81 EC C8 00 00 00 4C 8B 15", True),
    "RunScript": (PANORAMA, "4C 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 55 53 56 57 41 54 41 56 41 57 48 8D 6C 24 90 48 81 EC 70 01 00 00", True),
    "UpdateLightObject": (SCENE, "48 89 54 24 10 55 57 41 56 48 83 EC 50 48 8B 05 ?? ?? ?? ?? 4D 8B F0 48 8B FA 48 8B E9", True),
    "GlobalLightUpdate": (CLIENT, "4C 8B DC 57 48 81 EC C0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 8B F9 80 78 58 01 0F 84", True),
    "ToneMapUpdate": (SCENE, "48 8B 05 ?? ?? ?? ?? 0F 57 C0 F3 0F 10 1D ?? ?? ?? ?? 83 78 58 00 74 16 C7 81 88 00 00 00", True),
    "FogSetParamF": (CLIENT, "E8 ?? ?? ?? ?? F3 0F 10 9F 84 06 00 00 48 8D 4C 24 40", True),
    "CustomPaint_append": (CLIENT, "E8 ?? ?? ?? ?? 0F 28 B4 24 ?? ?? ?? ?? 4C 39 AD", True),
    "CustomPaint_material": (CLIENT, "48 8D 15 ?? ?? ?? ?? 48 8D 4D 00 E8 ?? ?? ?? ?? 48 8B D0 48 8D 4D 80", True),
    "pCSGOInput_1": (CLIENT, "48 8B 0D ?? ?? ?? ?? 4C 8B C6 8B 10 E8", False),
    "pCSGOInput_2": (CLIENT, "4C 8B 05 ?? ?? ?? ?? 41 8B 80 50 0B 00 00 85 C0", False),
    "UserCmdTable": (CLIENT, "48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B CF 4C 8B F8", True),
    "GetLocalPlayer": (CLIENT, "48 83 EC 28 83 F9 FF 75 ?? 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 30 48 8B 01 FF 90 ?? ?? ?? ?? 8B 08 48 63 C1 4C 8D 05", False),
    "GetBaseEntity": (CLIENT, "4C 8D 49 ?? 81 FA", False),
    "NetworkGameClient": (ENGINE, "48 89 3D ?? ?? ?? ?? FF 87", False),
    "EngineIsInGame": (ENGINE, "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 15 80 B8 ?? ?? ?? ?? 00 75 0C 83 B8 30 02 00 00 06", True),
    "EngineIsConnected": (ENGINE, "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 0B 83 B8 30 02 00 00 02 0F", False),
    "EngineBuildNumber": (ENGINE, "89 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 48 8B 0D", False),
}

FB_FIELDS = {
    "m_hGroundEntity": ("C_BaseEntity", "m_hGroundEntity"),
    "m_flStamina": ("CCSPlayer_MovementServices", "m_flStamina"),
    "m_arrForceSubtickMoveWhen": ("CPlayer_MovementServices", "m_arrForceSubtickMoveWhen"),
    "m_iHealth": ("C_BaseEntity", "m_iHealth"),
    "m_iTeamNum": ("C_BaseEntity", "m_iTeamNum"),
    "m_pGameSceneNode": ("C_BaseEntity", "m_pGameSceneNode"),
    "m_vecViewOffset": ("C_BaseModelEntity", "m_vecViewOffset"),
    "m_clrRender": ("C_BaseModelEntity", "m_clrRender"),
    "m_nRenderMode": ("C_BaseModelEntity", "m_nRenderMode"),
    "m_vOldOrigin": ("C_BasePlayerPawn", "m_vOldOrigin"),
    "m_pMovementServices": ("C_BasePlayerPawn", "m_pMovementServices"),
    "m_pWeaponServices": ("C_BasePlayerPawn", "m_pWeaponServices"),
    "m_vecAbsOrigin": ("CGameSceneNode", "m_vecAbsOrigin"),
    "m_modelState": ("CSkeletonInstance", "m_modelState"),
    "m_ModelName": ("CModelState", "m_ModelName"),
    "m_pAimPunchServices": ("C_CSPlayerPawn", "m_pAimPunchServices"),
    "m_iShotsFired": ("C_CSPlayerPawn", "m_iShotsFired"),
    "m_predictableBaseAngle": ("CCSPlayer_AimPunchServices", "m_predictableBaseAngle"),
    "m_predictableBaseAngleVel": ("CCSPlayer_AimPunchServices", "m_predictableBaseAngleVel"),
    "m_unpredictableBaseAngle": ("CCSPlayer_AimPunchServices", "m_unpredictableBaseAngle"),
    "m_pEntity": ("C_BaseEntity", "m_pEntity"),
    "m_hOwnerEntity": ("C_BaseEntity", "m_hOwnerEntity"),
    "m_designerName": ("CEntityIdentity", "m_designerName"),
    "m_pParent": ("CGameSceneNode", "m_pParent"),
    "m_flDetonateTime": ("C_BaseGrenade", "m_flDetonateTime"),
    "m_bombsiteCenterA": ("C_CSPlayerResource", "m_bombsiteCenterA"),
    "m_bombsiteCenterB": ("C_CSPlayerResource", "m_bombsiteCenterB"),
}


def parse_pattern(spec: str) -> tuple[bytes, bytes]:
    tokens = spec.split()
    needle = bytes(0 if t in {"?", "??"} else int(t, 16) for t in tokens)
    mask = bytes(0 if t in {"?", "??"} else 1 for t in tokens)
    return needle, mask


def scan(data: bytes, spec: str, limit: int = 16) -> list[int]:
    needle, mask = parse_pattern(spec)
    runs = list(re.finditer(b"\x01+", mask))
    if not runs:
        return []
    run = max(runs, key=lambda match: match.end() - match.start())
    fixed = run.start()
    anchor = needle[fixed:run.end()]
    out: list[int] = []
    pos = 0
    while len(out) < limit:
        found = data.find(anchor, pos + fixed)
        if found < 0:
            break
        start = found - fixed
        end = start + len(needle)
        if start >= 0 and end <= len(data) and all(
            not mask[i] or data[start + i] == needle[i] for i in range(len(needle))
        ):
            out.append(start)
        pos = found + 1 - fixed
    return out


def namespace_constants(text: str, namespace: str) -> dict[str, int]:
    marker = f"namespace {namespace}"
    start = text.find(marker)
    if start < 0:
        return {}
    brace = text.find("{", start)
    depth = 0
    end = -1
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    block = text[brace + 1 : end]
    return {
        name: int(value, 0)
        for name, value in re.findall(
            r"constexpr\s+(?:std::)?(?:u?int32_t|ptrdiff_t)\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)",
            block,
        )
    }


def class_fields(raw: dict, class_name: str) -> dict[str, int]:
    cls = raw.get("client.dll", {}).get("classes", {}).get(class_name, {})
    fields = cls.get("fields", {})
    out: dict[str, int] = {}
    for name, item in fields.items():
        if isinstance(item, dict) and "offset" in item:
            out[name] = int(item["offset"])
        elif isinstance(item, int):
            out[name] = item
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, default=GAME_BIN,
                        help="CS2 game directory (contains csgo/ and bin/)")
    parser.add_argument("--dump-dir", type=Path, default=DUMP_DIR,
                        help="Verified reference snapshot directory")
    args = parser.parse_args()
    failed = False
    source = OFFSETS_H.read_text(encoding="utf-8")
    offsets = json.loads((args.dump_dir / "offsets.json").read_text(encoding="utf-8"))
    schemas = json.loads((args.dump_dir / "client_dll.json").read_text(encoding="utf-8"))

    # Source and an old dump can agree while both are stale. Bind this reference
    # snapshot to the binaries that were actually inspected, not just a date.
    info = json.loads((args.dump_dir / "info.json").read_text(encoding="utf-8"))
    fingerprints = info.get("binary_sha256", {})
    print(f"[snapshot] build {info.get('build_number')} binary fingerprints")
    if not fingerprints:
        print("  FAIL snapshot has no verified binary fingerprints; refresh it against installed CS2")
        failed = True
    for relative, expected in sorted(fingerprints.items()):
        path = args.game_dir / relative
        actual = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
        ok = actual == expected
        print(f"  {'OK' if ok else 'FAIL':4} {relative}")
        failed |= not ok
    for required in ("csgo/bin/win64/client.dll", "bin/win64/engine2.dll"):
        if required not in fingerprints:
            print(f"  FAIL missing snapshot fingerprint: {required}")
            failed = True

    print("[offsets] Global namespace vs offsets.json")
    global_values = namespace_constants(source, "Global")
    expected_globals = dict(offsets["client.dll"])
    expected_globals.update(
        {f"engine_{k}": v for k, v in offsets["engine2.dll"].items() if k in {"dwBuildNumber", "dwNetworkGameClient", "dwNetworkGameClient_clientTickCount", "dwWindowHeight", "dwWindowWidth"}}
    )
    expected_globals["dwForceJump"] = json.loads((args.dump_dir / "buttons.json").read_text())["client.dll"]["jump"]
    for name, actual in sorted(global_values.items()):
        expected = expected_globals.get(name)
        ok = expected == actual
        print(f"  {'OK' if ok else 'FAIL':4} {name:36} source=0x{actual:X} dump={('-' if expected is None else f'0x{expected:X}')}")
        failed |= not ok

    prediction_header = (ROOT / "kAzeware/source/kAzeware/features/prediction/prediction.h").read_text(encoding="utf-8-sig")
    for constant, global_name in (("kRvaGlobalVars", "dwGlobalVars"), ("kRvaPrediction", "dwPrediction")):
        ok = bool(re.search(rf"\b{constant}\s*=\s*Offset::Global::{global_name}\s*;", prediction_header))
        print(f"  {'OK' if ok else 'FAIL':4} Pred::{constant} shares the verified global")
        failed |= not ok

    print("[offsets] FB namespace vs client_dll.json")
    fb_values = namespace_constants(source, "FB")
    for const, (cls, field) in FB_FIELDS.items():
        expected = class_fields(schemas, cls).get(field)
        actual = fb_values.get(const)
        # m_pEntity is a C++ base-layout identity pointer and is intentionally
        # absent from cs2-dumper's schema output. Keep its validated +0x10.
        if const == "m_pEntity" and expected is None:
            expected = 0x10
        ok = expected is not None and actual == expected
        print(f"  {'OK' if ok else 'FAIL':4} {const:30} source={('-' if actual is None else f'0x{actual:X}')} dump={('-' if expected is None else f'0x{expected:X}')} ({cls}->{field})")
        failed |= not ok

    print("[movement] schema-backed field bindings")
    movement_source = (ROOT / "kAzeware/source/kAzeware/features/movement/velocity_movement.cpp").read_text(encoding="utf-8-sig")
    for cls, field in re.findall(r'hash_32_fnv1a_const\("([A-Za-z_0-9]+)->([A-Za-z_0-9]+)"\)', movement_source):
        expected = class_fields(schemas, cls).get(field)
        ok = expected is not None and expected != 0
        print(f"  {'OK' if ok else 'FAIL':4} {cls}->{field} = {expected}")
        failed |= not ok

    print("[schema] source bindings vs refreshed client fields")
    fields: set[tuple[str, str]] = set()
    comments_and_strings = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', re.S)
    for path in (ROOT / "kAzeware/source").rglob("*"):
        if path.suffix not in {".h", ".hpp", ".c", ".cpp"}:
            continue
        text = path.read_text(encoding="utf-8-sig")
        text = comments_and_strings.sub(lambda m: m[0] if m[0].startswith('"') else "", text)
        fields.update(re.findall(r'"([A-Za-z_0-9]+)->([A-Za-z_0-9]+)"', text))
    for cls, field in sorted(fields):
        if (cls, field) == ("C_BaseEntity", "m_pEntity"):
            continue  # Native identity pointer, checked as +0x10 above.
        if field not in class_fields(schemas, cls):
            print(f"  FAIL missing source binding: {cls}->{field}")
            failed = True
    print(f"  checked {len(fields)} source field names (including native identity)")

    print("[paint] loose-variable fields and types vs client_dll.json")
    paint_source = (ROOT / "kAzeware/source/kAzeware/features/custom_paint/custom_paint.h").read_text(encoding="utf-8")
    paint_values = {name: int(value, 0) for name, value in re.findall(
        r"constexpr\s+(?:int|size_t)\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", paint_source)}
    paint_fields = class_fields(schemas, "CompositeMaterialInputLooseVariable_t")
    expected_paint = {name: paint_fields[field] for name, field in {
        "kOffName": "m_strName", "kOffType": "m_nVariableType",
        "kOffColor4": "m_cValueColor4", "kOffFloatX": "m_flValueFloatX",
        "kOffFloatY": "m_flValueFloatY", "kOffFloatZ": "m_flValueFloatZ",
    }.items()}
    paint_types = schemas["client.dll"]["enums"]["CompositeMaterialInputLooseVariableType_t"]["members"]
    expected_paint.update(kLooseVarColor4=paint_types["LOOSE_VARIABLE_TYPE_COLOR4"],
                          kLooseVarFloat3=paint_types["LOOSE_VARIABLE_TYPE_FLOAT3"])
    for name, expected in expected_paint.items():
        actual = paint_values.get(name)
        ok = actual == expected
        print(f"  {'OK' if ok else 'FAIL':4} {name:30} source={actual} dump={expected}")
        failed |= not ok

    print("[patterns] installed CS2 binaries")
    print(f"  game directory: {args.game_dir}")
    cache: dict[Path, bytes] = {}
    pattern_hits: dict[str, list[int]] = {}
    for name, (path, spec, required) in PATTERNS.items():
        path = args.game_dir / path.relative_to(GAME_BIN)
        if not path.is_file():
            print(f"  FAIL {name:20} missing binary: {path}")
            failed |= required
            continue
        if path not in cache:
            cache[path] = path.read_bytes()
        data = cache[path]
        hits = scan(data, spec)
        pattern_hits[name] = hits
        ok = len(hits) == 1
        level = "OK" if ok else ("FAIL" if required else "WARN")
        print(f"  {level:4} {name:20} count={len(hits):2} file_offsets={[hex(x) for x in hits]}")
        failed |= required and not ok

    # These pairs have source fallbacks; at least one pattern in each pair must hit.
    for group, names in {
        "pCSGOInput": ("pCSGOInput_1", "pCSGOInput_2"),
    }.items():
        ok = any(len(pattern_hits.get(n, [])) == 1 for n in names)
        print(f"  {'OK' if ok else 'FAIL':4} {group:20} fallback group")
        failed |= not ok

    # This getter's code can remain recognizable while the weapon field moves.
    # Verify its captured member displacement against the live-refreshed schema.
    hits = pattern_hits.get("GetSpread", [])
    if len(hits) == 1:
        displacement = int.from_bytes(cache[args.game_dir / CLIENT.relative_to(GAME_BIN)][hits[0] + 3:hits[0] + 7], "little")
        expected = class_fields(schemas, "C_CSWeaponBase").get("m_weaponMode")
        ok = displacement == expected
        print(f"  {'OK' if ok else 'FAIL':4} GetSpread weapon mode displacement: 0x{displacement:X}")
        failed |= not ok

    print("AUDIT_RESULT=" + ("FAIL" if failed else "PASS"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
