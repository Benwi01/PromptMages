# Materials for the spell effects (Niagara particles take their colour from Particle Color).
#   M_VFX_SoftGlow - additive soft dot for sprites (fire, sparks, sparkles, wind streaks).
#   M_VFX_Ribbon   - additive band, bright in the middle, for trails and the tether beam.
#   M_VFX_Chunk    - chunky lit mesh bits (smoke puffs, ice shards, embers); HDR particle colours glow.
# Run in the Unreal Editor (game NOT playing):  py "<path to this file>"   (safe to re-run)
import unreal

DEST = "/Game/PromptMages/VFX/Materials"
MEL = unreal.MaterialEditingLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def fresh(name):
    path = "%s/%s" % (DEST, name)
    if eal.does_asset_exist(path):
        mat = unreal.load_asset(path)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = assets.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    return mat


def node(mat, cls, x, y, **props):
    n = MEL.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def link(a, b, pin, a_out=""):
    MEL.connect_material_expressions(a, a_out, b, pin)


def usage(mat, sprites=False, ribbons=False, meshes=False):
    for flag, on in (("used_with_niagara_sprites", sprites), ("used_with_niagara_ribbons", ribbons),
                     ("used_with_niagara_mesh_particles", meshes)):
        mat.set_editor_property(flag, on)


def finish(mat):
    MEL.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log("PM VFX material %s" % mat.get_name())


def soft_glow():
    mat = fresh("M_VFX_SoftGlow")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    usage(mat, sprites=True, ribbons=True)
    pc = node(mat, unreal.MaterialExpressionParticleColor, -900, 0)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate, -900, 250)
    mid = node(mat, unreal.MaterialExpressionConstant2Vector, -900, 380, r=0.5, g=0.5)
    dist = node(mat, unreal.MaterialExpressionDistance, -700, 300)
    link(uv, dist, "A")
    link(mid, dist, "B")
    two = node(mat, unreal.MaterialExpressionMultiply, -550, 300, const_b=2.0)
    link(dist, two, "A")
    inv = node(mat, unreal.MaterialExpressionOneMinus, -420, 300)
    link(two, inv, "")
    sat = node(mat, unreal.MaterialExpressionSaturate, -300, 300)
    link(inv, sat, "")
    soft = node(mat, unreal.MaterialExpressionPower, -180, 300, const_exponent=1.6)
    link(sat, soft, "Base")
    alpha = node(mat, unreal.MaterialExpressionMultiply, -50, 200)
    link(pc, alpha, "A", "A")
    link(soft, alpha, "B")
    rgb = node(mat, unreal.MaterialExpressionMultiply, 100, 0)
    link(pc, rgb, "A", "RGB")
    link(alpha, rgb, "B")
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat)


def ribbon():
    mat = fresh("M_VFX_Ribbon")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    usage(mat, sprites=True, ribbons=True)
    pc = node(mat, unreal.MaterialExpressionParticleColor, -900, 0)
    uv = node(mat, unreal.MaterialExpressionTextureCoordinate, -900, 250)
    v = node(mat, unreal.MaterialExpressionComponentMask, -750, 250, r=False, g=True, b=False, a=False)
    link(uv, v, "")
    # sin(pi * v): 0 at the edges, 1 in the middle of the band.
    pi = node(mat, unreal.MaterialExpressionMultiply, -600, 250, const_b=3.14159)
    link(v, pi, "A")
    sine = node(mat, unreal.MaterialExpressionSine, -470, 250, period=6.28318)
    link(pi, sine, "")
    sat = node(mat, unreal.MaterialExpressionSaturate, -340, 250)
    link(sine, sat, "")
    soft = node(mat, unreal.MaterialExpressionPower, -220, 250, const_exponent=2.0)
    link(sat, soft, "Base")
    alpha = node(mat, unreal.MaterialExpressionMultiply, -80, 200)
    link(pc, alpha, "A", "A")
    link(soft, alpha, "B")
    rgb = node(mat, unreal.MaterialExpressionMultiply, 80, 0)
    link(pc, rgb, "A", "RGB")
    link(alpha, rgb, "B")
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat)


def chunk():
    mat = fresh("M_VFX_Chunk")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    usage(mat, meshes=True, sprites=True)
    pc = node(mat, unreal.MaterialExpressionParticleColor, -900, 0)
    sat = node(mat, unreal.MaterialExpressionSaturate, -650, -100)
    link(pc, sat, "", "RGB")
    MEL.connect_material_property(sat, "", unreal.MaterialProperty.MP_BASE_COLOR)
    glow = node(mat, unreal.MaterialExpressionMultiply, -650, 60, const_b=0.6)
    link(pc, glow, "A", "RGB")
    MEL.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = node(mat, unreal.MaterialExpressionConstant, -650, 200, r=0.8)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    # Fades out with particle alpha by dithering (keeps it cheap and opaque-looking).
    dither = node(mat, unreal.MaterialExpressionMaterialFunctionCall, -450, 320)
    dither.set_material_function(unreal.load_asset("/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA"))
    link(pc, dither, "Alpha Threshold", "A")
    MEL.connect_material_property(dither, "Result", unreal.MaterialProperty.MP_OPACITY_MASK)
    finish(mat)


def save_mesh(mesh, name):
    unreal.GeometryScript_Normals.set_per_face_normals(mesh)  # Flat-shaded facets for the low-poly look.
    path = "/Game/PromptMages/VFX/Meshes/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    o = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    o.enable_recompute_normals = False
    o.enable_collision = False
    sm, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, path, o)
    sm.set_material(0, unreal.load_asset(DEST + "/M_VFX_Chunk"))
    eal.save_loaded_asset(sm)
    unreal.log("PM VFX mesh " + path)


def meshes():
    GS = unreal.GeometryScript_Primitives
    opts = unreal.GeometryScriptPrimitiveOptions()
    one = unreal.Vector(1, 1, 1)
    # Puff: a faceted ball, 100 cm across (like the engine sphere, so mesh scales read the same).
    puff = unreal.DynamicMesh()
    GS.append_sphere_lat_long(puff, opts, unreal.Transform(), 50.0, 5, 7, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    save_mesh(puff, "SM_VFX_Puff")
    # Shard: a four-sided crystal, 40 cm long, centred on its middle.
    shard = unreal.DynamicMesh()
    GS.append_cone(shard, opts, unreal.Transform(unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0), one),
                   8.0, 0.0, 26.0, 4, 1, False, unreal.GeometryScriptPrimitiveOriginMode.BASE)
    GS.append_cone(shard, opts, unreal.Transform(unreal.Vector(0, 0, 0), unreal.Rotator(roll=180, pitch=0, yaw=0), one),
                   8.0, 0.0, 14.0, 4, 1, False, unreal.GeometryScriptPrimitiveOriginMode.BASE)
    save_mesh(shard, "SM_VFX_Shard")


soft_glow()
ribbon()
chunk()
meshes()
unreal.log("PM VFX materials done")
