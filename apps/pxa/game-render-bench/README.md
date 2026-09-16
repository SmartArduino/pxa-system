# GameRender Bench

This device benchmark runs nine four-second groups, then leaves the final
result list on screen:

- RGB565 full-screen clear baseline (`2D-CL`);
- 192 native-size INDEX8 sprites in one sprite-batch record (`2D-SN`);
- 192 variably scaled INDEX8 sprites in one sprite-batch record (`2D-SS`);
- 192 transparent, additive INDEX8 sprites (`2D-AD`);
- 96 animated RGB565 flat quads (`2D-QD`);
- 24 perspective-correct, depth-tested textured quads (`2D-TX`);
- 3 rotating, depth-tested cubes with back-face culling (`3D-03`);
- 6 rotating, depth-tested cubes with back-face culling (`3D-06`);
- 12 rotating, depth-tested cubes with back-face culling (`3D-12`).

The result list reports `V/R/MS`: visible FPS, rendered FPS, and average Host
raster time per rendered frame. Comparing visible and rendered FPS exposes the
display/presentation ceiling separately from raster throughput. Cyan marks 2D
groups and amber marks 3D groups. Benchmark frames contain only the workload
under test, without a live HUD. The app requests 16 ms clock ticks and relies
on GameRender's latest-wins mailbox when rendering or presentation cannot keep
up.
