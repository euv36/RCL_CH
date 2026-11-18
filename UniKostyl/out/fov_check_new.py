import cv2, numpy as np

FULL = r"D:\camera\UniKostyl\out\fov_new\fov_new_full.jpg"
SMALL = r"D:\camera\UniKostyl\out\fov_new\fov_new.jpg"

ref = cv2.imread(FULL, cv2.IMREAD_GRAYSCALE)
img = cv2.imread(SMALL, cv2.IMREAD_GRAYSCALE)
rh, rw = ref.shape
h, w = img.shape
print("ref", rw, rh, "| small", w, h, "| expected scale", rw / w, rh / h)

orb = cv2.ORB_create(nfeatures=8000, fastThreshold=10)
kr, dr = orb.detectAndCompute(ref, None)
km, dm = orb.detectAndCompute(img, None)
print("keypoints ref/small:", len(kr), len(km))

matcher = cv2.BFMatcher(cv2.NORM_HAMMING, crossCheck=True)
matches = sorted(matcher.match(dm, dr), key=lambda m: m.distance)
good = matches[: min(1500, len(matches))]
print("matches:", len(matches), "used:", len(good))

src = np.float32([km[m.queryIdx].pt for m in good]).reshape(-1, 1, 2)
dst = np.float32([kr[m.trainIdx].pt for m in good]).reshape(-1, 1, 2)
H, mask = cv2.findHomography(src, dst, cv2.RANSAC, 3.0)
inl = int(mask.sum()) if mask is not None else 0
corners = np.float32([[0, 0], [w, 0], [w, h], [0, h]]).reshape(-1, 1, 2)
tc = cv2.perspectiveTransform(corners, H).reshape(-1, 2)
x0, y0 = tc.min(axis=0)
x1, y1 = tc.max(axis=0)
sx = np.linalg.norm(H[:, 0][:2])
sy = np.linalg.norm(H[:, 1][:2])
print(f"inliers={inl} scale=({sx:.4f},{sy:.4f})")
print(f"mapped bbox=({x0:.1f},{y0:.1f})-({x1:.1f},{y1:.1f})")
print(f"fraction of full frame=({(x1-x0)/rw:.4f},{(y1-y0)/rh:.4f})")
print("corners:", np.round(tc, 1).tolist())
