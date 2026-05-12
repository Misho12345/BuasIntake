BuasIntake
==========

Theme
-----
The game is based on the assignment theme "Transform".

The player transforms a small generated planet by digging terrain away, placing stored terrain back, moving pond water, planting seeds, collecting resources, and upgrading tools. The world starts as a rough rocky planet with caves and ponds, then gradually becomes a greener restored planet through player changes.

Goal
----
Restore the planet by growing vegetation over the exposed surface.

The game tracks green surface coverage, not every terrain sample inside the planet. When enough exposed outer terrain has become green, the planet is restored and the win overlay appears.

How to Play
-----------
Walk around the planet, dig into the terrain, collect resources from rocks and ores, and spend those resources on tool upgrades.

The terrain tool can dig ground and place stored ground back. The water bucket can pick up water and preview where water will be placed before committing it. Seeds can be planted only on valid wet ground, and mature plants spread over time.

Controls
--------
A / D: move left and right along the planet surface.
W / Space: jump.
Mouse wheel: switch between tool slots.
Ctrl + mouse wheel: zoom the camera in and out.

Terrain tool:
Left mouse button: dig terrain, or collect a nearby resource if one is targeted.
Right mouse button: place stored terrain.

Water bucket:
Left mouse button: pick up water when not in placement mode.
Right mouse button: enter water placement preview mode.
Mouse wheel in preview mode: change how much water will be placed.
Right mouse button in preview mode: confirm water placement.
Left mouse button or Escape in preview mode: cancel water placement.

Seed tool:
Left mouse button: plant a seed on valid wet ground.

Upgrade menu:
E: open or close the upgrade menu.
Escape: close the upgrade menu and cancel the active tool interaction.
Left mouse button in the upgrade menu: buy an available upgrade.

Main Systems
------------
Terrain generation runs mostly on the GPU. A compute shader creates the base planet density field, then cave and pond passes modify that field. Water is smoothed before the terrain and water surfaces are extracted.

The terrain and water meshes are generated with a marching-squares style pipeline. One compute pass finds the iso-surface edge crossings, and another pass builds the render mesh and boundary edges used for colliders.

The CPU reads the generated field back so gameplay systems can query terrain, water, resources, wetness, greenness, and plant placement. Terrain edits mark affected samples and nearby chunks dirty, then only those chunks rebuild their meshes, water surfaces, and colliders.

Resources and vegetation are attached to terrain samples or resolved surface anchors. This keeps ores, rocks, dead plants, and living plants connected to the generated terrain instead of floating at grid centers.

The HUD is split between the resource counter, the tool hotbar, the water placement preview, the goal progress bar, and the upgrade menu. The tool controller owns tool selection and upgrade routing, while the HUD renderers only draw the current state.

Build Instructions
------------------
Open BuasIntake.sln in Visual Studio 2022 or Visual Studio Community and build the project.

The project is a Windows-first C++ project using the Visual Studio v143 toolset and the latest C++ language mode configured in the .vcxproj file. Build outputs go under bin/<Platform>/<Configuration>/.

Attribution and References
--------------------------
Libraries used by the project include SFML, Box2D, GLAD, and OpenGL.

The terrain generation and contour extraction were inspired by:
https://www.youtube.com/watch?v=0ZONMNUKTfU
https://www.youtube.com/watch?v=PLMcCKeJ6f0
https://jorisar.github.io/portfolio/posts/smoothvoxelterrain/
https://www.youtube.com/watch?v=M3iI2l0ltbE
https://github.com/SebLague/Marching-Cubes

The terrain mesh generation was adapted from previous Surface Nets and Marching Cubes work into the current marching-squares style mesh and collider edge extraction. AI assistance was used while combining and debugging parts of these procedural algorithms.

Vegetation tree and bush pixel art was generated with an adapted procedural approach inspired by:
https://github.com/archaicvirus/TreeGenerator
https://tic80.com/play?cart=3424

AI assistance was also used for some vegetation generation details, especially growth transition sprites, bugfixing, and quality checks.

The UI and HUD for placement, construction, and upgrades used AI assistance for straightforward layout and wiring work.

Other art references/assets include:
https://craftpix.net/freebies/free-mining-pixel-32x32-icons/?srsltid=AfmBOoo8QOt4NMsxRJmkfqBjfIPpX6RtGqQRkyqRqKTyHgw1FNENbqC2
https://opengameart.org/content/tileable-200x200-dirt-texture

Project assets are stored in the assets folder.
