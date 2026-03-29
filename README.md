
# LGX: High-Performance Multi-Source Log Viewer

## 1. Vision & Goals

The goal of this project is to build a **modern, high-performance log viewer for Linux (and eventually cross-platform)** that scales from single multi-million-line log files to **live, remote, and structured log sources** such as Docker, Loki, and other streaming backends.

The application should feel closer to an **IDE for logs** than a traditional text viewer:

* Fast, responsive UI regardless of log size
* Multiple windows and tabs
* Strong support for structured logs (JSON)
* Extensible log source architecture
* Direct support for log-streams from Docker, Systemd, Android (for connected devices).
* Correct parsing and coloring for common log formats. 

Basically a log-viewer for lazy developers and devops people.

## Status:

**Under initial development**

