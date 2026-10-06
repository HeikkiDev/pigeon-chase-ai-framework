# Yolo-Fastest-1.1 (COCO) — provenance

The model ADR-0019 chose, vendored rather than fetched: a build that reaches
the network is not reproducible and does not work offline (`REQ-DEV-002`).
685 KB is the one-off cost in git for a file that changes only when the model
is retrained.

## Upstream

| | |
| --- | --- |
| Project | Yolo-Fastest, by dog-qiuqiu |
| Repository | <https://github.com/dog-qiuqiu/Yolo-Fastest> |
| Branch | `master` |
| Retrieved | 2026-10-06 |
| Licence | YOLO LICENSE Version 2 (verbatim in `LICENSE` next to this file) |
| Classes | COCO 80. Only `bird` survives the seam (ADR-0019 §4, ruling Q23) |
| Input | 320 × 320 RGB, normalised by 1/255, no mean subtraction |
| Blobs | input `data`, output `output` (`Yolov3DetectionOutput`) |

## Files

| File | Retrieved from | Bytes | SHA-256 |
| --- | --- | --- | --- |
| `yolo-fastest-1.1.param` | <https://raw.githubusercontent.com/dog-qiuqiu/Yolo-Fastest/master/sample/ncnn/model/yolo-fastest-1.1.param> | 17909 | `3924d5c7e1945ae3b86e958f7335b730828659be9bc6355784823974a92793c9` |
| `yolo-fastest-1.1.bin` | <https://raw.githubusercontent.com/dog-qiuqiu/Yolo-Fastest/master/sample/ncnn/model/yolo-fastest-1.1.bin> | 666952 | `ffc755fd530847c96926d3e675c5c1bf63b392e2c034773acd89921447edbc5b` |
| `LICENSE` | <https://raw.githubusercontent.com/dog-qiuqiu/Yolo-Fastest/master/LICENSE> | 515 | `2021f0b8d6426fbf9a2f0126f1e8a7d3b1d7027c871b9979fa2543b9557000c4` |

The two model digests are repeated in `checksums.sha256`, which
`raspberry/CMakeLists.txt` verifies at configure time (ADR-0019 §8): a
corrupted or swapped bundle then fails the build instead of silently changing
what the system shoots at.

## The baked-in threshold

The `Yolov3DetectionOutput` layer in `yolo-fastest-1.1.param` carries
`2=5.500000e-01`, so the model itself discards anything below 0.55. ADR-0019
§5 treats that as a floor: `NcnnDetectorSettings::confidence_threshold` is
applied on top of it and may only raise it. Lowering the configured value
below 0.55 therefore has no effect, which is why the threshold is edited here
in the model file by nobody and in the calibration file by the maintainer.

## Replacing the model

Retraining to a single `pigeon` class is optional work (Q23) and changes no
interface. Whoever does it replaces both files, regenerates
`checksums.sha256` with `shasum -a 256`, updates the table above, and checks
whether the label constant in `raspberry/src/ncnn_detector.cpp` still names
the right class.
