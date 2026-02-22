This folder stores chess piece textures used by the renderer.

Current set path:
assets/pieces/staunton/

Flipped set path (used when board view rotates to black side):
assets/pieces/staunton_flipped/

Expected files (PNG):
- wp.png
- wn.png
- wb.png
- wr.png
- wq.png
- wk.png
- bp.png
- bn.png
- bb.png
- br.png
- bq.png
- bk.png

Expected flipped files (PNG, same names):
- wp.png
- wn.png
- wb.png
- wr.png
- wq.png
- wk.png
- bp.png
- bn.png
- bb.png
- br.png
- bq.png
- bk.png

Renderer fallback:
- If these files are missing, the app falls back to built-in vector pieces.
- If flipped files are missing, renderer reuses normal textures with a 180-degree piece rotation.
