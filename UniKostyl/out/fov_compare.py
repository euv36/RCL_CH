import cv2, numpy as np, glob, os, json

ref = cv2.imread(r"D:\camera\UniKostyl\out\cam_bench\1920x1080.jpg", cv2.IMREAD_GRAYSCALE)
rh, rw = ref.shape
orb = cv2.ORB_create(nfeatures=8000, fastThreshold=10)
kr, dr = orb.detectAndCompute(ref, None)
print("ref", rw, rh, "keypoints", len(kr))

matcher = cv2.BFMatcher(cv2.NORM_HAMMING, crossCheck=True)
results = {}
for p in sorted(glob.glob(r"D:\camera\UniKostyl\out\cam_bench\*.jpg")):
    name = os.path.basename(p)
    if name.startswith("1920"):
        continue
    img = cv2.imread(p, cv2.IMREAD_GRAYSCALE)
    h, w = img.shape
    km, dm = orb.detectAndCompute(img, None)
    matches = matcher.match(dm, dr)
    matches = sorted(matches, key=lambda m: m.distance)
    good = matches[: min(1500, len(matches))]
    if len(good) < 10:
        print(name, "not enough matches", len(good)); continue
    src = np.float32([km[m.queryIdx].pt for m in good]).reshape(-1, 1, 2)
    dst = np.float32([kr[m.trainIdx].pt for m in good]).reshape(-1, 1, 2)
    H, mask = cv2.findHomography(src, dst, cv2.RANSAC, 3.0)
    inl = int(mask.sum()) if mask is not None else 0
    corners = np.float32([[0,0],[w,0],[w,h],[0,h]]).reshape(-1,1,2)
    tc = cv2.perspectiveTransform(corners, H).reshape(-1,2)
    x0,y0 = tc.min(axis=0); x1,y1 = tc.max(axis=0)
    # scale factor from homography
    sx = np.linalg.norm(H[:,0][:2]); sy = np.linalg.norm(H[:,1][:2])
    print(f"{name:14s} {w}x{h} matches={len(good)} inliers={inl} "
          f"scale~({sx:.4f},{sy:.4f}) mapped_bbox=({x0:.0f},{y0:.0f})-({x1:.0f},{y1:.0f}) "
          f"frac_of_ref=({(x1-x0)/rw:.3f},{(y1-y0)/rh:.3f})")
    results[name] = dict(w=w,h=h,matches=len(good),inliers=inl,sx=float(sx),sy=float(sy),
                         bbox=[float(x0),float(y0),float(x1),float(y1)])
print(json.dumps(results, indent=1))
