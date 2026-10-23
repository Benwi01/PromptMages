# Builds the low-poly mage costume as static mesh assets + colour material instances.
# Pieces are modelled in the demake's rest pose space (cm, feet at Z=0, facing +Y: the toes point +Y), so attaching each one to its
# bone with the inverse of that bone's rest transform puts it exactly where it was modelled.
# Run in the Unreal Editor:  py "<path to this file>"   (safe to re-run: it overwrites the assets)
import math
import unreal

DEST = "/Game/PromptMages/Mage"
PARENT = unreal.load_asset("/Game/PromptMages/Materials/M_LowPoly")
GS = unreal.GeometryScript_Primitives
assets = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary

COLORS = {
    "Robe": unreal.LinearColor(0.16, 0.05, 0.34, 1.0),   # wizard purple
    "Trim": unreal.LinearColor(0.85, 0.6, 0.08, 1.0),    # gold
    "Belt": unreal.LinearColor(0.3, 0.16, 0.06, 1.0),    # leather brown
    "Beard": unreal.LinearColor(0.95, 0.95, 0.93, 1.0),  # old-wizard grey
    "Hat": unreal.LinearColor(0.12, 0.04, 0.28, 1.0),    # slightly darker purple
}


def material(name):
    path = DEST + "/MI_Mage_" + name
    if eal.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = assets.create_asset("MI_Mage_" + name, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, PARENT)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "DiffuseColor", COLORS[name])
    eal.save_loaded_asset(mi)
    return mi


def xf(loc=(0, 0, 0), rot=None, scale=(1, 1, 1)):
    r = rot if rot is not None else unreal.Rotator(0, 0, 0)
    return unreal.Transform(unreal.Vector(*loc), r, unreal.Vector(*scale))


def along(a, b):
    """Rotation that turns +Z to point from a to b."""
    d = unreal.Vector(b[0] - a[0], b[1] - a[1], b[2] - a[2])
    return unreal.MathLibrary.make_rot_from_z(d)


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def build(name, parts, mat):
    mesh = unreal.DynamicMesh()
    opts = unreal.GeometryScriptPrimitiveOptions()
    for kind, transform, kw in parts:
        if kind == "cone":
            GS.append_cone(mesh, opts, transform, kw["r0"], kw["r1"], kw["h"], kw.get("steps", 6), 1, True, unreal.GeometryScriptPrimitiveOriginMode.BASE)
        else:
            GS.append_cylinder(mesh, opts, transform, kw["r"], kw["h"], kw.get("steps", 6), 0, True, unreal.GeometryScriptPrimitiveOriginMode.BASE)
    path = DEST + "/SM_Mage_" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    o = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    o.enable_recompute_normals = False
    o.enable_collision = False
    sm, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, path, o)
    sm.set_material(0, mat)
    eal.save_loaded_asset(sm)
    unreal.log("PM built " + path)


robe, trim, belt, beard, hat = (material(n) for n in ("Robe", "Trim", "Belt", "Beard", "Hat"))

# --- Hat (bone DEF-head). Head top is ~182 cm, head centre ~Y -2; the face is towards +Y. ---
crown_base, crown_tip = (0, -3, 178.5), (0, -9.5, 204.7)       # leans back a little
flop_tip = (0, -22, 197)                                        # floppy tip droops down behind
build("Hat", [
    ("cyl", xf((0, -2, 176.5)), dict(r=24, h=2.5, steps=10)),                                   # brim
    ("cone", xf(crown_base, along(crown_base, crown_tip)), dict(r0=13, r1=4, h=27, steps=7)),   # crown
    ("cone", xf(crown_tip, along(crown_tip, flop_tip)), dict(r0=4.2, r1=0.3, h=15, steps=6)),   # floppy tip
], hat)
build("HatBand", [("cyl", xf((0, -2, 178.6)), dict(r=13.4, h=3.5, steps=7))], trim)

# --- Beard (bone DEF-head): a downward pyramid from the chin, flattened front-to-back, jutting forward over the robe. ---
chin, beard_tip = (0, 7, 166), (0, 21, 140)
build("Beard", [("cone", xf(chin, along(chin, beard_tip), (1.0, 0.7, 1.0)), dict(r0=9, r1=0.8, h=29, steps=5))], beard)

# --- Robe top in two pieces so it bends with the spine (one rigid piece pokes out when the mage stoops). ---
build("RobeChest", [("cyl", xf((0, 0, 127), None, (1.15, 0.97, 1.0)), dict(r=22.5, h=31, steps=6))], robe)  # bone DEF-spine_003
build("RobeBelly", [("cyl", xf((0, -1, 98), None, (1.1, 0.95, 1.0)), dict(r=20.5, h=33, steps=6))], robe)    # bone DEF-spine_001

# --- Robe skirt (bone DEF-spine, the hips): flares out to the knees. ---
build("RobeSkirt", [("cone", xf((0, -1, 104), unreal.Rotator(roll=180, pitch=0, yaw=0), (1.0, 1.0, 1.0)), dict(r0=20, r1=31, h=54, steps=6))], robe)

# --- Belt (bone DEF-spine). ---
build("Belt", [("cyl", xf((0, -1, 99), None, (1.2, 1.05, 1.0)), dict(r=19.5, h=5.5, steps=6))], belt)

# --- Sleeves and bell-shaped cuffs (bones DEF-upper_arm_* and DEF-forearm_*). ---
for side, sx in (("L", 1), ("R", -1)):
    shoulder = (16.5 * sx, -10.4, 153.5)
    elbow = (37.2 * sx, -11.5, 126.4)
    wrist = (57.7 * sx, -0.5, 110.9)
    build("Sleeve" + side, [("cone", xf(lerp(shoulder, elbow, -0.1), along(shoulder, elbow)), dict(r0=8, r1=6.8, h=38, steps=6))], robe)
    build("ForeSleeve" + side, [("cone", xf(lerp(elbow, wrist, -0.08), along(elbow, wrist)), dict(r0=6.8, r1=6.6, h=19, steps=6))], robe)
    build("Cuff" + side, [("cone", xf(lerp(elbow, wrist, 0.55), along(elbow, wrist)), dict(r0=6.5, r1=9.5, h=11, steps=6))], trim)

# Older single-piece robe top, replaced by RobeChest + RobeBelly.
if eal.does_asset_exist(DEST + "/SM_Mage_RobeTop"):
    eal.delete_asset(DEST + "/SM_Mage_RobeTop")

unreal.log("PM mage costume done")
