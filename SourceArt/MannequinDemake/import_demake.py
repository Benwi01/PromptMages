# Imports the low-poly mannequin demake as a skeletal mesh and writes a report about it.
# Run inside the Unreal Editor:  py "<path to this file>"
import json
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
FBX = os.path.join(HERE, "ue_mannequin_demake.fbx")
DEST = "/Game/Characters/MannequinDemake"
REPORT = os.path.join(HERE, "import_report.json")

report = {"fbx": FBX, "errors": []}
try:
    task = unreal.AssetImportTask()
    task.filename = FBX
    task.destination_path = DEST
    task.automated = True
    task.save = True
    task.replace_existing = True

    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_as_skeletal = True
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_animations = False
    options.import_materials = True
    options.import_textures = True
    options.create_physics_asset = False
    task.options = options

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    report["imported"] = list(task.imported_object_paths)

    for path in report["imported"]:
        asset = unreal.load_asset(path)
        if isinstance(asset, unreal.SkeletalMesh):
            report["mesh"] = path
            bounds = asset.get_bounds()
            report["bounds_origin"] = [bounds.origin.x, bounds.origin.y, bounds.origin.z]
            report["bounds_extent"] = [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z]
            skeleton = asset.skeleton
            report["skeleton"] = skeleton.get_path_name() if skeleton else None
            report["materials"] = [m.material_interface.get_path_name() if m.material_interface else None for m in asset.materials]
except Exception as e:  # noqa
    report["errors"].append(repr(e))

with open(REPORT, "w") as f:
    json.dump(report, f, indent=2)
unreal.log("PM import report written to " + REPORT)
