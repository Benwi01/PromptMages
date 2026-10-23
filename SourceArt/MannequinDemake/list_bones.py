# Writes the imported demake skeleton's bone names (as Unreal sees them) next to this file.
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
mesh = unreal.load_asset("/Game/Characters/MannequinDemake/ue_mannequin_demake")
pose = unreal.AnimPoseExtensions.get_reference_pose(mesh.skeleton)
names = [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(pose)]
with open(os.path.join(HERE, "bones.txt"), "w") as f:
    f.write("\n".join(names))
unreal.log("PM wrote %d bone names" % len(names))
