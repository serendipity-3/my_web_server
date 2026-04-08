# Web Server Performance Test Report

**Date**: 2026-03-08
**Server**: my-web-server (C++ epoll + thread pool)
**Test Environment**: localhost

## Configuration
- wrk 命令行测试工具
- backlog: 2048
- Thread pool: 8 threads (hardware_concurrency)

## Test Results (Python urllib)

wrk -t8 -c8 -d10s http://localhost:8888/
Running 20s test @ http://localhost:8888/
8 threads and 8 connections
Thread Stats   Avg      Stdev     Max   +/- Stdev
Latency     3.19ms    1.51ms  35.33ms   71.80%
Req/Sec   308.85     33.47   585.00     72.06%
49267 requests in 20.04s, 4.89MB read
Requests/sec:   2457.91
Transfer/sec:    249.63KB


wrk -t8 -c16 -d10s http://localhost:8888/
Running 10s test @ http://localhost:8888/
8 threads and 16 connections
Thread Stats   Avg      Stdev     Max   +/- Stdev
Latency    14.09ms   43.56ms 400.69ms   96.72%
Req/Sec   292.62     45.02   434.00     84.15%
22814 requests in 10.02s, 2.26MB read
Requests/sec:   2275.73
Transfer/sec:    231.13KB

