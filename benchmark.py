#!/usr/bin/env python3
import urllib.request
import threading
import time
from collections import defaultdict
import statistics

NUM_THREADS = 8
REQUESTS_PER_THREAD = 2500
TOTAL_REQUESTS = NUM_THREADS * REQUESTS_PER_THREAD
URL = "http://localhost:8888/index.html"

latencies = []
errors = 0
errors_lock = threading.Lock()
latencies_lock = threading.Lock()
completed = 0
completed_lock = threading.Lock()

def make_requests(count):
    global errors, completed
    local_latencies = []
    local_errors = 0
    
    for _ in range(count):
        start = time.time()
        try:
            urllib.request.urlopen(URL, timeout=10)
            latency = (time.time() - start) * 1000
            local_latencies.append(latency)
        except Exception as e:
            local_errors += 1
        
        with completed_lock:
            completed += 1
            if completed % 5000 == 0:
                print(f"Progress: {completed}/{TOTAL_REQUESTS}")
    
    with latencies_lock:
        latencies.extend(local_latencies)
    with errors_lock:
        errors += local_errors

print(f"Starting performance test...")
print(f"Threads: {NUM_THREADS}, Requests: {TOTAL_REQUESTS}")
print("-" * 40)

start_time = time.time()

threads = []
for _ in range(NUM_THREADS):
    t = threading.Thread(target=make_requests, args=(REQUESTS_PER_THREAD,))
    threads.append(t)
    t.start()

for t in threads:
    t.join()

elapsed = time.time() - start_time

print("\n" + "=" * 40)
print("RESULTS")
print("=" * 40)
print(f"Total requests:  {TOTAL_REQUESTS}")
print(f"Completed:        {len(latencies)}")
print(f"Errors:           {errors}")
print(f"Total time:       {elapsed:.2f}s")
print(f"QPS:              {TOTAL_REQUESTS/elapsed:.0f}/s")

if latencies:
    latencies.sort()
    p50 = latencies[int(len(latencies) * 0.50)]
    p95 = latencies[int(len(latencies) * 0.95)]
    p99 = latencies[int(len(latencies) * 0.99)]
    
    print(f"\nLatency (ms):")
    print(f"  Min:     {min(latencies):.2f}")
    print(f"  Max:     {max(latencies):.2f}")
    print(f"  Avg:     {statistics.mean(latencies):.2f}")
    print(f"  Median:  {statistics.median(latencies):.2f}")
    print(f"  P95:     {p95:.2f}")
    print(f"  P99:     {p99:.2f}")
