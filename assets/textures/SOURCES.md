# Textures in this folder

| File | Used by | Source | Licence |
| --- | --- | --- | --- |
| natural_walnut_veneer_diff_1k.jpg | the grandfather clock's walnut (tools/blender/make_models.py, FILE_TEXTURES) | https://polyhaven.com/a/natural_walnut_veneer (diffuse map, 1k) | CC0 (Poly Haven) |
| knitted_fleece_diff_1k.jpg (original), knitted_fleece_diff_512.jpg (downscaled copy) and knitted_fleece_wool_512.jpg (grey, brightened, low-contrast copy of it: the one used) | the Forgetting's wool: core, strand pieces, tips and yarn (FILE_TEXTURES in make_models.py) | https://polyhaven.com/a/knitted_fleece (diffuse map, 1k) | CC0 (Poly Haven); the derived copies are ours |

## Ground kit (added for the 3D stage ground, item 2 of the ground upgrade)

All three are CC0 from Poly Haven (https://polyhaven.com), 1k diffuse maps, downloaded through api.polyhaven.com/files/<slug>.

| File | Source URL | Licence |
| --- | --- | --- |
| aerial_grass_rock_diff_1k.jpg | https://polyhaven.com/a/aerial_grass_rock | CC0 |
| forest_ground_04_diff_1k.jpg | https://polyhaven.com/a/forest_ground_04 | CC0 |
| rock_face_diff_1k.jpg | https://polyhaven.com/a/rock_face | CC0 |
| ground_grass_512.jpg | derived from aerial_grass_rock_diff_1k.jpg | CC0 (derived: 512px, 75% desaturated, brightened to mean 0.95, contrast x0.8) |
| ground_earth_512.jpg | derived from forest_ground_04_diff_1k.jpg | CC0 (derived: 512px, 70% desaturated, brightened, contrast x0.8) |
| ground_rock_512.jpg | derived from rock_face_diff_1k.jpg | CC0 (derived: 512px, 85% desaturated, brightened, contrast x0.8) |

The derived copies are what the game loads (Models3D baked ground): the shader multiplies them with a per-vertex colour, so they are kept grey and bright.
