# U²-Net-p (vendored model)

Cut Out (the editor's selection button) lifts the subject of a selection with this salient-object model when Windows'
own `ImageObjectExtractor` is not available (Platform/SubjectMask). It is embedded in Eikon.exe as an
`RCDATA` resource (Eikon.rc) and run on the CPU with the ONNX Runtime of the Windows App SDK framework
(Windows ML), so the package carries no runtime of its own.

- Model: **U²-Net-p** ("u2netp", the small 4.4 MiB variant of U²-Net), Qin et al., *U²-Net: Going Deeper
  with Nested U-Structure for Salient Object Detection*, Pattern Recognition 106, 2020.
  Weights: https://github.com/xuebinqin/U-2-Net (Apache License 2.0, `LICENSE`, unchanged).
- File: `u2netp.onnx`, the ONNX export published by rembg (MIT):
  https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx
  4,574,861 bytes, SHA-256 `309c8469258dda742793dce0ebea8e6dd393174f89934733ecc8b14c76f4ddd8`.
  Opset 11; input `input.1` [1, 3, 320, 320]; the first output (`1959`, [1, 1, 320, 320]) is the
  saliency map.
- Pre/post-processing (Core/CutOut): rembg's, i.e. resize to 320 × 320, divide by the brightest value,
  ImageNet mean/std; the map is min-max stretched and scaled back up, then Eikon tightens the edge
  (smoothstep 0.3–0.7) and feathers it by about one device pixel.

## Updating

Replace `u2netp.onnx` (keep the name, or change Eikon.rc), update the size and hash above, and run Cut
Out on a few test images (a gradient background, a photo, a UI screenshot).
