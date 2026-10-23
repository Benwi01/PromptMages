# Builds the apprentice wand: a tapered wooden shaft, a leather grip with a pommel, gold rings and a glowing crystal.
# Modelled along +Z with the grip centre (where the fist holds it) at the origin; the crystal tip ends at Z = 34 cm,
# which is the wand component's default WandLength.
# Run in the Unreal Editor (game NOT playing):  py "<path to this file>"   (safe to re-run)
import unreal

DEST = "/Game/PromptMages/Wand"
LOWPOLY = unreal.load_asset("/Game/PromptMages/Materials/M_LowPoly")
GS = unreal.GeometryScript_Primitives
MEL = unreal.MaterialEditingLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def flat_mi(name, rgb):
    path = DEST + "/MI_Wand_" + name
    mi = unreal.load_asset(path) if eal.does_asset_exist(path) else assets.create_asset(
        "MI_Wand_" + name, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, LOWPOLY)
    MEL.set_material_instance_vector_parameter_value(mi, "DiffuseColor", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    eal.save_loaded_asset(mi)
    return mi


def glow_material():
    """M_WandGlow: a lit crystal whose colour (parameter "Color", same name the spell objects use) also glows."""
    path = DEST + "/M_WandGlow"
    if eal.does_asset_exist(path):
        mat = unreal.load_asset(path)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = assets.create_asset("M_WandGlow", DEST, unreal.Material, unreal.MaterialFactoryNew())
    color = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.4, 0.1, 1.0))
    glow = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, 200)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 6.0)
    mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -350, 150)
    MEL.connect_material_expressions(color, "", mul, "A")
    MEL.connect_material_expressions(glow, "", mul, "B")
    MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -350, 300)
    rough.set_editor_property("r", 0.25)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def xf(z, flip=False):
    rot = unreal.Rotator(roll=180, pitch=0, yaw=0) if flip else unreal.Rotator(0, 0, 0)
    return unreal.Transform(unreal.Vector(0, 0, z), rot, unreal.Vector(1, 1, 1))


def build(name, parts, mat):
    mesh = unreal.DynamicMesh()
    opts = unreal.GeometryScriptPrimitiveOptions()
    for kind, transform, kw in parts:
        if kind == "cone":
            GS.append_cone(mesh, opts, transform, kw["r0"], kw["r1"], kw["h"], kw.get("steps", 6), 1, True,
                           unreal.GeometryScriptPrimitiveOriginMode.BASE)
        else:
            GS.append_cylinder(mesh, opts, transform, kw["r"], kw["h"], kw.get("steps", 6), 0, True,
                               unreal.GeometryScriptPrimitiveOriginMode.BASE)
    path = DEST + "/SM_Wand_" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    o = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    o.enable_recompute_normals = False
    o.enable_collision = False
    sm, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, path, o)
    sm.set_material(0, mat)
    eal.save_loaded_asset(sm)
    unreal.log("PM built " + path)


wood = flat_mi("Wood", (0.20, 0.10, 0.045))
leather = flat_mi("Grip", (0.06, 0.03, 0.022))
gold = unreal.load_asset("/Game/PromptMages/Mage/MI_Mage_Trim")
glow = glow_material()

# Grip: wrapped handle through the fist, with a knob (pommel) under the hand.
build("Grip", [
    ("cyl", xf(-6.0), dict(r=1.3, h=11.0, steps=6)),
    ("cone", xf(-6.0, flip=True), dict(r0=1.3, r1=1.9, h=1.8, steps=6)),   # flares out under the fist
    ("cone", xf(-7.8, flip=True), dict(r0=1.9, r1=0.6, h=1.0, steps=6)),   # rounded-off end
], leather)
# Shaft: tapers from the grip to the crystal.
build("Shaft", [("cone", xf(5.0), dict(r0=1.0, r1=0.42, h=25.0, steps=6))], wood)
# Gold rings: top of the grip, bottom of the grip, and the collar holding the crystal.
build("Rings", [
    ("cyl", xf(4.6), dict(r=1.55, h=1.1, steps=6)),
    ("cyl", xf(-6.4), dict(r=1.5, h=0.9, steps=6)),
    ("cone", xf(29.4), dict(r0=0.55, r1=0.95, h=1.1, steps=6)),
], gold)
# Crystal: a pointed gem, glowing in the selected spell's colour.
build("Crystal", [
    ("cone", xf(30.4), dict(r0=1.1, r1=0.05, h=3.6, steps=6)),
    ("cone", xf(30.4, flip=True), dict(r0=1.1, r1=0.3, h=0.9, steps=6)),
], glow)

unreal.log("PM wand done")
