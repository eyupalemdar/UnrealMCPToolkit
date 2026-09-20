# UI texture minification and bounded atlas sampling

This is the project-neutral recipe. Host art approval, original-density probe,
TSpec and packaging rules remain authoritative.

1. Identify the active consumer and its actual physical draw range, DPI and
   framebuffer. Trace the skin/MIC/MID rather than editing an unused base asset.
2. Preserve approved original pixels and provenance. Select colour RGBA, linear
   mask, packed data or font handling explicitly. Do not re-key/despill approved
   filtered alpha. Generate each authored mip from the same original/source box.
3. For regular atlases, filter each cell separately. Every authored level must
   contain whole cells of at least 4x4. Declare the maximum sampled LOD. Engine
   runtime mip tails may continue past that limit and mix cells.
4. Use the AIAssetPipeline material sampling recipe to bind the existing UV and
   explicit cell vector. Clamp LOD and clamp cell UVs by half a texel at the
   coarser trilinear level. Keep art composition and widget geometry unchanged.
5. Verify imported source and effective runtime dimensions, source/runtime mip
   counts, sRGB, compression, filter, LOD group and source hashes. A group's
   MaxLODSize can discard the intended top levels. Re-read the material graph.
6. Capture the same art/state at identical physical pixel sizes, then test the
   real target package and active consumers. Tune bias per project after this
   comparison. Existing native-size/no-mip assets can remain valid for measured
   roles; there is no universal preset for every UI texture.

Implementation, CLI, schema and engineering smoke:
`Plugins/AIAssetPipeline/Docs/IMAGE_QUALITY.md` in the consuming project.
The bootstrap's `doctor --strict` verifies installation; its `quality-smoke`
command separately tests packaging and optional native render. Never describe
installation success as visual or cooked-device acceptance.

Model generation, including Image 2.5, supplies approved sources and actual
provenance metadata. The downstream contract is independent of the model and
the coding agent. Record decisions in project recipes and receipts.
