"""Offline check of per-pixel masking on the Pi program (no camera needed)."""
import numpy as np

import rpi_elp_cam as R

H, W = 360, 640
tracker = R.GateTracker(W, H, (40, 100, 100), 8, 1.0, 2)
tracker.update_gate_from_rgb(0, (255, 255, 0))   # yellow
tracker.update_gate_from_rgb(1, (0, 0, 255))     # blue

frame = np.full((H, W, 3), 40, np.uint8)          # dark grey BGR background
frame[100:160, 200:300] = (0, 255, 255)           # BGR of yellow blob
frame[250:300, 400:470] = (255, 0, 0)             # BGR of blue blob
before = frame.copy()

import cv2
cv2.cvtColor(frame, cv2.COLOR_BGR2HSV, dst=tracker.hsv)

overlay = ((0, 255, 0), (255, 0, 255))
found = []
for g in range(tracker.gate_count):
    info = tracker.process(g)
    if info is None:
        print(f"gate {g+1}: not found")
        continue
    found.append(info)
    np.greater(tracker.masks[g], 0, out=tracker.mask_bool)
    n_mask = int(tracker.mask_bool.sum())
    tracker.draw(frame, g, info, overlay[g])
    painted = np.any(frame != before, axis=2)
    changed = int(painted.sum())
    print(f"gate {g+1}: angle={info['angle']} area={info['area_px']:.0f} "
          f"mask_px={n_mask} changed_px={changed}")

# verify: every changed pixel is either the blob area or the small label text
changed = np.any(frame != before, axis=2)
ys, xs = np.nonzero(changed)
print("changed bbox:", xs.min(), ys.min(), xs.max(), ys.max())
# the old rectangle would have painted a 1px border spanning the whole bbox;
# per-pixel masking should not paint the area outside the thresholded blobs
outside = changed.copy()
for (y0, y1, x0, x1) in [(100, 160, 200, 300), (250, 300, 400, 470)]:
    outside[y0:y1, x0:x1] = False
print("changed pixels outside blobs (label text only):", int(outside.sum()))

# corners must be untouched -> no rectangle
print("corner pixel unchanged:", bool((frame[0, 0] == before[0, 0]).all()))
print("rect border pixel unchanged:", bool((frame[100, 200] == before[100, 200]).all()) is False)
print("frame dtype/range ok:", frame.dtype, int(frame.min()), int(frame.max()))
print("OK" if len(found) == 2 and int(outside.sum()) < 400 else "CHECK FAILED")
