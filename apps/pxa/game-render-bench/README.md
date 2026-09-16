# GameRender Bench

This device benchmark runs four five-second groups, then leaves the final
result list on screen:

- 192 moving INDEX8 sprites in one sprite-batch record (`2D-SP`);
- 96 animated RGB565 flat quads (`2D-QD`);
- 6 rotating, depth-tested cubes with back-face culling (`3D-06`);
- 12 rotating, depth-tested cubes with back-face culling (`3D-12`).

The centered top overlay reports actual visible FPS and the most recent Host
raster time. The result list reports each group's visible FPS and average Host
raster time per rendered frame. Cyan marks 2D groups and amber marks 3D groups.
The app requests 16 ms clock ticks and relies on GameRender's latest-wins
mailbox when rendering or presentation cannot keep up.
