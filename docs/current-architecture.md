# PhoneCam C++ Architecture Current State

> Last Updated: 2026-07-19
> Python backend completely retired. C++ Qt6 backend is the new active main branch.

## 1. End-To-End Pipeline

```
┌─────────────────────────────────────────────────────────────────────┐
│  Android Phone (phone_native/)                                      │
│                                                                     │
│  Camera2 ImageReader (YUV_420_888)                                  │
│       │                                                             │
│       ▼                                                             │
│  Yuv420Extractor.imageToI420()                                      │
│       │                                                             │
│       ▼                                                             │
│  EglRenderer.drawYuv() → MediaCodec InputSurface                    │
│       │                                                             │
│       ▼                                                             │
│  H264Encoder (dequeueOutputLoop thread)                             │
│       │                                                             │
│       ▼                                                             │
│  PcpPacketWriter (32-byte header: PHCM/v2/video/h264/...)           │
│       │                                                             │
│       ▼                                                             │
│  TcpStreamServer.sendPacket()                                       │
│                                                                     │
│  ──── TCP port 9999 (adb reverse / WiFi) ────                     │
└─────────────────────────────────────────┬───────────────────────────┘
                                          │
┌─────────────────────────────────────────▼───────────────────────────┐
│  Windows Desktop (cpp/)                                             │
│                                                                     │
│  StreamReceiver (QTcpSocket)                                        │
│       │  Reads 32-byte header + NAL units (H.264)                   │
│       ▼                                                             │
│  DecoderThread (FFmpeg HW decode)                                   │
│       │  avcodec_send_packet / avcodec_receive_frame                │
│       │  D3D11VA HW decode → NV12 frame                             │
│       ▼                                                             │
│  SharedMemoryWriter (Windows File Mapping)                          │
│       │  Writes NV12 frame to "Local\\PhoneCam_SharedVideo"         │
│       │  Signaling via Event "Local\\PhoneCam_VideoEvent"           │
│       ▼                                                             │
│  ──── IPC ────                                                      │
└─────────────────────────────────────────┬───────────────────────────┘
                                          │
┌─────────────────────────────────────────▼───────────────────────────┐
│  DirectShow Virtual Camera (cpp/virtual_cam_module/)                │
│                                                                     │
│  Filter: CVCam (inherits CSource)                                   │
│  Pin: CVCamStream (inherits CSourceStream)                          │
│       │                                                             │
│       │  SharedMemoryReader::read() polls Event                     │
│       │  Reads NV12 frame -> Copies to DirectShow payload           │
│       ▼                                                             │
│  Tencent WeMeet / OBS / WebRTC (loads virtual_cam_module.dll)       │
└─────────────────────────────────────────────────────────────────────┘
```

## 2. Directory Structure

```
D:\PhoneCam\
├── phone_native/       # Android App (Kotlin, Camera2, MediaCodec)
├── cpp/                # Desktop C++ App
│   ├── build.bat       # CMake + Ninja + MSVC builder
│   ├── CMakeLists.txt
│   ├── src/            # Qt GUI + TCP network + FFmpeg decoder
│   │   ├── main.cpp
│   │   ├── MainWindow.cpp / .h
│   │   ├── StreamReceiver.cpp / .h
│   │   ├── DecoderThread.cpp / .h
│   │   ├── SharedMemoryWriter.cpp / .h
│   │   └── utils.h
│   └── virtual_cam_module/  # DirectShow Filter DLL
│       ├── dllmain.cpp
│       ├── filter.cpp / .h  # CVCam and CVCamStream
│       └── baseclasses/     # DirectShow BaseClasses (strmbase.lib)
├── vcpkg/              # C++ dependencies (Qt6, FFmpeg)
└── docs/               # Architecture and plans
```
