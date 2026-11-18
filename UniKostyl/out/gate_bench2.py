#!/usr/bin/env python3
import sys
sys.argv = ["x"]
exec(open("/home/twox/gate_bench.py").read().split("run(2, \"640x360\", \"40,100,100\"")[0])
run(6, "640x360", "90,255,255", "6 ворот ПОСЛЕ оптимизации")
run(2, "640x360", "40,100,100", "2 ворот реальный")
run(6, "480x270", "90,255,255", "6 ворот 480x270")
print("done2", flush=True)
