# Builds M_MageBody: paints the mage's robe straight onto the skinned body, so it bends with it and never clips.
# Colours come from the body's rest-pose position (cm, feet at Z=0, facing +Y):
#   below the shins -> boots, shins to neck (except hands) -> robe, head and hands -> skin.
# Flat-shaded like M_LowPoly. The demake body's material instances are re-parented to it.
# Run in the Unreal Editor:  py "<path to this file>"   (safe to re-run)
import unreal

DEST = "/Game/PromptMages/Mage"
NAME = "M_MageBody"
MEL = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary

ROBE_TOP_Z = 157.0     # neck
BOOT_TOP_Z = 50.0      # just below the skirt's hem
HAND_X = 56.0          # wrists are at |X| ~58

path = DEST + "/" + NAME
if eal.does_asset_exist(path):
    mat = unreal.load_asset(path)
    MEL.delete_all_material_expressions(mat)
else:
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, DEST, unreal.Material, unreal.MaterialFactoryNew())

mat.set_editor_property("tangent_space_normal", False)
mat.set_editor_property("used_with_skeletal_mesh", True)


def node(cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def colour(name, rgb, y):
    p = node(unreal.MaterialExpressionVectorParameter, -700, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return p


pos = node(unreal.MaterialExpressionPreSkinnedPosition, -700, -200)
robe = colour("RobeColor", (0.16, 0.05, 0.34), 0)
skin = colour("SkinColor", (0.86, 0.86, 0.84), 200)
boots = colour("BootColor", (0.12, 0.07, 0.04), 400)

custom = node(unreal.MaterialExpressionCustom, -350, 0)
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
custom.set_editor_property("description", "MageBodyColour")
inputs = []
for n in ("P", "Robe", "Skin", "Boots"):
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", n)
    inputs.append(ci)
custom.set_editor_property("inputs", inputs)
custom.set_editor_property("code", (
    "float ax = abs(P.x);\n"
    "if (P.z < %.1f) return Boots.rgb;\n"
    "if (P.z < %.1f && ax < %.1f) return Robe.rgb;\n"
    "return Skin.rgb;\n") % (BOOT_TOP_Z, ROBE_TOP_Z, HAND_X))
# The rest-pose position only exists per vertex; carry it to the pixel shader.
interp = node(unreal.MaterialExpressionVertexInterpolator, -520, -200)
ok = MEL.connect_material_expressions(pos, "", interp, "VS") or MEL.connect_material_expressions(pos, "", interp, "")
ok = MEL.connect_material_expressions(interp, "", custom, "P") and ok
if not ok:
    unreal.log_error("PM body material: could not wire the rest position")
MEL.connect_material_expressions(robe, "", custom, "Robe")
MEL.connect_material_expressions(skin, "", custom, "Skin")
MEL.connect_material_expressions(boots, "", custom, "Boots")
MEL.connect_material_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)

rough = node(unreal.MaterialExpressionConstant, -350, 250)
rough.set_editor_property("r", 0.85)
MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

# Flat shading: face normal from screen-space derivatives of the world position (same as M_LowPoly).
wp = node(unreal.MaterialExpressionWorldPosition, -900, 500)
ddx = node(unreal.MaterialExpressionDDX, -700, 480)
ddy = node(unreal.MaterialExpressionDDY, -700, 560)
cross = node(unreal.MaterialExpressionCrossProduct, -500, 520)
norm = node(unreal.MaterialExpressionNormalize, -350, 520)
MEL.connect_material_expressions(wp, "", ddx, "")
MEL.connect_material_expressions(wp, "", ddy, "")
MEL.connect_material_expressions(ddx, "", cross, "A")
MEL.connect_material_expressions(ddy, "", cross, "B")
MEL.connect_material_expressions(cross, "", norm, "")
MEL.connect_material_property(norm, "", unreal.MaterialProperty.MP_NORMAL)

MEL.recompile_material(mat)
eal.save_loaded_asset(mat)

# The body's two material instances (Body, Joints) switch over to this material.
for mi_path in ("/Game/Characters/MannequinDemake/Body", "/Game/Characters/MannequinDemake/Joints"):
    mi = unreal.load_asset(mi_path)
    MEL.set_material_instance_parent(mi, mat)
    eal.save_loaded_asset(mi)
unreal.log("PM mage body material done")
