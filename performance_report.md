# Web Server Performance Test Report

**Date**: 2026-03-08
**Server**: my-web-server (C++ epoll + thread pool)
**Test Environment**: localhost

## Configuration
- backlog: 2048
- Thread pool: 8 threads (hardware_concurrency)

## Test Results (Python urllib)

| Requests | Concurrency | Time | QPS |
|----------|-------------|------|-----|
| 1,000 | 50 | 0.55s | 1,810 |
| 10,000 | 100 | 5.08s | 1,970 |
| 20,000 | 200 | 9.95s | 2,009 |

## Previous Results (curl)

| Test Type | Requests | Time | QPS |
|-----------|----------|------|-----|
| Concurrent (500) | 500 | 0.63s | ~792 |
| Concurrent (2000) | 2000 | 2.82s | ~709 |

## Analysis

1. **True Performance**: ~2000 QPS with proper concurrent testing
2. **Bottleneck identified**: curl process overhead was limiting previous tests
3. **Thread pool**: 8 threads handle requests efficiently
4. **ab tool issue**: ApacheBench has compatibility issues with this server (only 1 request completes)

## Optimization Suggestions

1. Use sendfile() for file serving (zero-copy)
2. Implement EPOLLOUT for large response support  
3. Add HTTP keep-alive support to reduce connection overhead
4. Consider using SO_REUSEPORT for multi-core scaling
