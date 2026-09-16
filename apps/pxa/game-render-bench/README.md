# GameRender Bench

This device benchmark alternates every five seconds between:

- 192 moving INDEX8 sprites in one sprite-batch record;
- 12 rotating, depth-tested cubes in three triangle-batch records.

The top strip is intentionally numeric-free so the benchmark does not measure
font rendering. Cyan means 2D and amber means 3D. The green bar shows measured
frame submissions up to 60 fps; the red bar shows Host raster time up to 30 ms.
The app requests 16 ms clock ticks and relies on GameRender's latest-wins
mailbox when rendering or presentation cannot keep up.
