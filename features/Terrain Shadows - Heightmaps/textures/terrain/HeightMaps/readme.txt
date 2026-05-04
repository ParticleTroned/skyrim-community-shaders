These DDS files use the xLODGen-style terrain heightmap naming scheme and must
install to:

Data\textures\terrain\HeightMaps\

Required filename format:

[worldspace editorID].Terrain.HeightMap.[West cell].[South cell].[East cell].[North cell].[z min].[z max].dds

Example:

DLC2SolstheimWorld.Terrain.HeightMap.-64.-64.127.127.-1024.6342.dds

Do not move these files to Data\textures\heightmaps\. TerrainShadows only loads
.Terrain.HeightMap files while scanning Data\textures\terrain\ subdirectories.
The Data\textures\heightmaps\ directory uses the separate original format:

[worldspace editorID].HeightMap.[West cell].[South cell].[East cell].[North cell].[z black].[z white].[z min].[z max].dds
