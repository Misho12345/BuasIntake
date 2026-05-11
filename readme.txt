BuasIntake

Theme
-----
The game is based on the theme "Transform". The player changes a small planet by digging terrain away, placing it somewhere else, moving water, planting seeds, and upgrading tools. The world gradually transforms from a rough generated planet into a shaped, watered, and planted environment.

How to Play
-----------
Walk around the planet, collect resources, and use them to improve your tools. Terrain can be removed and placed back, ponds can be moved with the bucket, and seeds can be planted on suitable wet ground. The main goal is to reshape the planet and use the resources you find to keep improving what you can do.

Controls
--------
A / D: Move left and right along the planet surface.
W / Space: Jump.
Mouse wheel: Switch between tool slots.
Left mouse button with terrain tool: Dig terrain or collect a nearby resource.
Right mouse button with terrain tool: Place stored terrain.
Left mouse button with water bucket: Pick up or place water, depending on bucket mode.
Right mouse button with water bucket: Cancel the current water placement.
Left mouse button with seed tool: Plant a seed on valid wet ground.
E: Open or close the upgrade menu.
Left mouse button in upgrade menu: Buy an upgrade if enough resources are available.
Escape: Close the upgrade menu or cancel the active interaction.
Ctrl + mouse wheel: Zoom the camera.

Main Systems
------------
Terrain generation starts on the GPU. A compute shader builds the planet density field, cave and pond passes modify it, and a smoothing pass cleans up water values. Marching-squares shaders then extract terrain and water meshes from the field. The CPU reads back the field so gameplay code can query terrain, water, resources, and plant placement.

Terrain editing is batched while the player drags the mouse. This avoids rebuilding chunks for every single brush stamp. Edited samples mark nearby chunks dirty, then those chunks rebuild their meshes and colliders.

Resources are attached to terrain samples so they stay connected to the generated caves and surface. Vegetation uses surface anchors as well, so plants sit on the planet instead of floating at grid centers.

Build Instructions
------------------
Open BuasIntake.sln in Visual Studio Community and build the project as x64. The project should be tested in both Debug|x64 and Release|x64 before submission.

Run the game from the Visual Studio output folder so relative paths to assets resolve correctly.

Attribution
-----------
Libraries used by the project include SFML, Box2D, and GLAD/OpenGL. They are included as project dependencies rather than as a game engine.

The project assets are stored in the assets folder. If any asset is replaced before submission, update this section with the original creator, source link, and license.

Known Limitations
-----------------
Water is field-based and gameplay-focused rather than a full fluid simulation. The planet size is fixed for this assignment build. These choices keep the game stable and make terrain editing, water placement, resources, and vegetation work together clearly.
