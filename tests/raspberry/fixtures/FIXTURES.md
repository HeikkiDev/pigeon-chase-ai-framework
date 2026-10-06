# Test fixtures — provenance and licence

Every image here is **CC0 / public domain**. ADR-0019 forbids anything else:
an attribution obligation that travels with a test fixture becomes a licence
obligation on every downstream user of this repository, for no engineering
benefit, and unlicensed images (the images.cv set the detector spike used) do
not enter a public repository at all.

The licence of each file below was read on its Wikimedia Commons file page and
confirmed through the Commons API (`extmetadata.LicenseShortName`) on
2026-10-05.

## Images

All images are stored as **binary PPM (`P6`)**: a short ASCII header followed
by packed RGB888, top row first — the `REQ-DET-004` byte layout with no
decoding library and therefore no new dependency (AGENTS.md rule 12). Row
stride equals `width × 3`; the loader in `support/ppm_fixture.hpp` re-pads a
frame when a test needs `stride > width × 3`.

### `pigeon_640x480.ppm`

| | |
| --- | --- |
| Source | <https://commons.wikimedia.org/wiki/File:Pigeon_in_London.jpg> |
| Author | Sebalston (own work) |
| Licence | CC0 1.0 Universal Public Domain Dedication |
| Original | JPEG, 5184 × 3456 |
| Conversion | `sips -z 480 640` (stretch to the 640 × 480 capture resolution of maintainer ruling Q24, aspect ratio not preserved) → 24-bit BMP → PPM, channel order BGR → RGB |
| Measured | The Yolo-Fastest-1.1 COCO model reports label 15 (`bird`) with probability **0.9670**, normalised box (0.3331, 0.2261)–(0.9016, 0.8639), identically at one and four threads over repeated runs |

A single rock dove filling most of the frame, in daylight, on a stone ledge —
the nominal case the deployment rig exists for. Its measured confidence is far
above the model's baked-in 0.55 floor, so the suite's threshold is not a
tuning parameter anybody has to calibrate.

### `brick_wall_320x240.ppm`

| | |
| --- | --- |
| Source | <https://commons.wikimedia.org/wiki/File:Brick_wall_-_Arlington,_MA_-_01.jpg> |
| Author | Daderot (own work) |
| Licence | CC0 1.0 Universal Public Domain Dedication |
| Original | JPEG, 4032 × 2268 |
| Conversion | `sips -z 240 320` → 24-bit BMP → PPM, channel order BGR → RGB |
| Measured | The model reports **no detections at all** (0 output rows) |

A building façade with windows: the no-bird scene a balcony camera sees for
most of its life.

### `car_320x240.ppm`

| | |
| --- | --- |
| Source | <https://commons.wikimedia.org/wiki/File:Car_parked_at_the_northeast_viewpoint_at_Alexandra_Park,_Bath_2025-07-22.jpg> |
| Author | Andy Li (own work) |
| Licence | CC0 1.0 Universal Public Domain Dedication |
| Original | JPEG, 8160 × 6144 |
| Conversion | `sips -z 240 320` → 24-bit BMP → PPM, channel order BGR → RGB |
| Measured | The model reports label 3 (`car`) with probability **0.9605** and **no `bird`** |

The proxy-class case: the model finds something it is confident about, and the
detector must still answer `NONE`, because only the COCO `bird` class survives
the seam (ADR-0019 §4). Without this fixture, "`FOUND` means a bird" is
indistinguishable from "`FOUND` means the model saw anything".

## Deliberately corrupt model files

Not images and not licensed from anywhere: a few bytes written by hand so that
the load-failure statuses of ADR-0019 §3 have something to fail on. They must
never be confused with the vendored model bundle.

| File | Content | Used for |
| --- | --- | --- |
| `malformed_model.param` | One line of ASCII; the ncnn magic number is absent | `ModelLoadStatus::MALFORMED` on the topology file |
| `malformed_model.bin` | 1032 bytes of repeated ASCII | `ModelLoadStatus::MALFORMED` on the weights file |

## Reproducing the conversion

No conversion tool is committed; the images are. The steps above used only
macOS's built-in `sips` and a throwaway BMP-to-PPM script, so the fixtures can
be regenerated from the original URLs if anyone needs to. Regenerating them is
**not** part of any build: `REQ-DEV-002` requires the suite to run with no
network access, which is why the pixels are in git.
