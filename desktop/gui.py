#!/usr/bin/env python3
"""PhoneCam Desktop GUI

tkinter 桌面 GUI，显示连接状态和摄像头预览。
"""

import tkinter as tk
from tkinter import ttk
import threading
import time
import logging
from typing import Optional
from PIL import Image, ImageTk
import cv2
import numpy as np

from receiver import MjpegReceiver
from virtual_camera import VirtualCamera
from connection_manager import ConnectionManager, ConnectionState, ConnectionInfo

logger = logging.getLogger(__name__)


class PhoneCamGUI:
    """PhoneCam 桌面 GUI"""

    def __init__(self):
        self.root = tk.Tk()
        self.root.title("PhoneCam")
        self.root.geometry("800x600")
        self.root.minsize(640, 480)
        self.root.configure(bg='#1a1a2e')

        # 状态
        self._manager: Optional[ConnectionManager] = None
        self._receiver: Optional[MjpegReceiver] = None
        self._vcam: Optional[VirtualCamera] = None
        self._is_connected = False
        self._current_frame: Optional[np.ndarray] = None
        self._frame_lock = threading.Lock()

        # 配置
        self._width = 640
        self._height = 480
        self._fps = 15

        self._build_ui()
        self._start_discovery()

    def _build_ui(self):
        """构建界面"""
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('Title.TLabel', font=('Microsoft YaHei', 14, 'bold'),
                        background='#1a1a2e', foreground='white')
        style.configure('Status.TLabel', font=('Microsoft YaHei', 11),
                        background='#16213e', foreground='#a0a0a0')
        style.configure('Info.TLabel', font=('Consolas', 10),
                        background='#16213e', foreground='#00d4aa')
        style.configure('Action.TButton', font=('Microsoft YaHei', 11))

        # ── 顶部状态栏 ──
        status_frame = tk.Frame(self.root, bg='#16213e', height=60)
        status_frame.pack(fill='x', padx=0, pady=0)
        status_frame.pack_propagate(False)

        self._status_icon = tk.Label(status_frame, text="🔍", font=('', 20),
                                     bg='#16213e', fg='white')
        self._status_icon.pack(side='left', padx=15)

        status_text_frame = tk.Frame(status_frame, bg='#16213e')
        status_text_frame.pack(side='left', fill='y')

        self._status_title = ttk.Label(status_text_frame, text="正在搜索设备...",
                                       style='Title.TLabel')
        self._status_title.pack(anchor='w', pady=(8, 0))

        self._status_detail = ttk.Label(status_text_frame, text="请确保手机 App 已启动推流",
                                        style='Status.TLabel')
        self._status_detail.pack(anchor='w')

        # 分辨率选择
        res_frame = tk.Frame(status_frame, bg='#16213e')
        res_frame.pack(side='right', padx=15)

        ttk.Label(res_frame, text="分辨率:", style='Status.TLabel').pack(side='left')
        self._res_var = tk.StringVar(value="640x480")
        res_combo = ttk.Combobox(res_frame, textvariable=self._res_var,
                                 values=["320x240", "640x480", "1280x720"],
                                 state='readonly', width=10)
        res_combo.pack(side='left', padx=5)
        res_combo.bind('<<ComboboxSelected>>', self._on_resolution_change)

        # ── 预览区域 ──
        preview_frame = tk.Frame(self.root, bg='#0a0a1a')
        preview_frame.pack(fill='both', expand=True, padx=10, pady=10)

        self._canvas = tk.Canvas(preview_frame, bg='#0a0a1a', highlightthickness=0)
        self._canvas.pack(fill='both', expand=True)

        # 初始占位文字
        self._canvas.create_text(
            400, 250, text="等待连接...",
            font=('Microsoft YaHei', 16), fill='#404060', tags='placeholder'
        )

        # ── 底部控制栏 ──
        bottom_frame = tk.Frame(self.root, bg='#16213e', height=50)
        bottom_frame.pack(fill='x', padx=0, pady=0)
        bottom_frame.pack_propagate(False)

        # 连接信息
        self._info_label = ttk.Label(bottom_frame, text="", style='Info.TLabel')
        self._info_label.pack(side='left', padx=15)

        # 按钮
        btn_frame = tk.Frame(bottom_frame, bg='#16213e')
        btn_frame.pack(side='right', padx=15)

        self._mirror_btn = ttk.Button(btn_frame, text="镜像", width=6,
                                      command=self._toggle_mirror, style='Action.TButton')
        self._mirror_btn.pack(side='left', padx=5)

        self._flip_btn = ttk.Button(btn_frame, text="翻转", width=6,
                                    command=self._toggle_flip, style='Action.TButton')
        self._flip_btn.pack(side='left', padx=5)

        self._quit_btn = ttk.Button(btn_frame, text="退出", width=6,
                                    command=self._quit, style='Action.TButton')
        self._quit_btn.pack(side='left', padx=5)

        # 翻转/镜像状态
        self._mirror = False
        self._flip = False

        # 帧率统计
        self._frame_count = 0
        self._last_fps_time = time.time()
        self._display_fps = 0.0

        # 定时更新 UI
        self._update_loop()

    def _start_discovery(self):
        """启动设备发现"""
        self._manager = ConnectionManager(port=8080)
        self._manager.on_state_change(self._on_connection_change)
        self._manager.start()

    def _on_connection_change(self, info: ConnectionInfo):
        """连接状态变化回调（非 UI 线程）"""
        self.root.after(0, self._update_status_ui, info)

    def _update_status_ui(self, info: ConnectionInfo):
        """在 UI 线程更新状态"""
        if info.state == ConnectionState.SEARCHING:
            self._status_icon.config(text="🔍")
            self._status_title.config(text="正在搜索设备...")
            self._status_detail.config(text="请确保手机 App 已启动推流")
        elif info.state == ConnectionState.CONNECTED:
            self._status_icon.config(text="✅")
            self._status_title.config(text=f"已连接 ({info.connection_type})")
            self._status_detail.config(text=info.url)

            if not self._is_connected:
                self._is_connected = True
                self._start_receiver(info.url)
        elif info.state == ConnectionState.DISCONNECTED:
            self._status_icon.config(text="❌")
            self._status_title.config(text="连接断开")
            self._status_detail.config(text="正在重连...")

    def _start_receiver(self, url: str):
        """启动视频接收"""
        if self._receiver:
            self._receiver.stop()

        self._receiver = MjpegReceiver(url)
        self._receiver.on_frame(self._on_frame)
        self._receiver.start()

        # 启动虚拟摄像头
        if not self._vcam:
            self._vcam = VirtualCamera(
                width=self._width, height=self._height, fps=self._fps
            )
            if not self._vcam.open():
                logger.warning("虚拟摄像头打开失败")
                self._vcam = None

    def _on_frame(self, frame: np.ndarray):
        """收到新帧（非 UI 线程）"""
        with self._frame_lock:
            self._current_frame = frame

        # 发送到虚拟摄像头
        if self._vcam and self._vcam.is_open:
            self._vcam.send(frame)

        self._frame_count += 1

    def _update_loop(self):
        """定时更新 UI"""
        # 更新预览
        with self._frame_lock:
            frame = self._current_frame

        if frame is not None:
            self._display_frame(frame)

        # 更新 FPS
        now = time.time()
        elapsed = now - self._last_fps_time
        if elapsed >= 1.0:
            self._display_fps = self._frame_count / elapsed
            self._frame_count = 0
            self._last_fps_time = now

        # 更新信息栏
        info_parts = []
        if self._receiver:
            info_parts.append(f"接收: {self._receiver.fps:.0f}fps")
        if self._vcam and self._vcam.is_open:
            info_parts.append(f"虚拟摄像头: {self._vcam.device_name}")
        info_parts.append(f"显示: {self._display_fps:.0f}fps")
        self._info_label.config(text=" | ".join(info_parts))

        self.root.after(33, self._update_loop)  # ~30fps UI 刷新

    def _display_frame(self, frame: np.ndarray):
        """在 Canvas 上显示帧"""
        try:
            # 镜像/翻转
            if self._mirror:
                frame = cv2.flip(frame, 1)
            if self._flip:
                frame = cv2.flip(frame, 0)

            # BGR -> RGB
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

            # 缩放到 canvas 大小
            canvas_w = self._canvas.winfo_width()
            canvas_h = self._canvas.winfo_height()
            h, w = rgb.shape[:2]
            if canvas_w > 1 and canvas_h > 1:
                scale = min(canvas_w / w, canvas_h / h)
                new_w = int(w * scale)
                new_h = int(h * scale)
                rgb = cv2.resize(rgb, (new_w, new_h))

            # 转为 PhotoImage
            img = Image.fromarray(rgb)
            photo = ImageTk.PhotoImage(image=img)

            # 清除占位文字，显示图像
            self._canvas.delete('placeholder')
            self._canvas.delete('preview')
            self._canvas.create_image(
                canvas_w // 2, canvas_h // 2,
                image=photo, anchor='center', tags='preview'
            )
            self._canvas._photo = photo  # 防止 GC
        except Exception as e:
            logger.debug(f"显示帧失败: {e}")

    def _toggle_mirror(self):
        self._mirror = not self._mirror
        self._mirror_btn.config(text="镜像 ✓" if self._mirror else "镜像")

    def _toggle_flip(self):
        self._flip = not self._flip
        self._flip_btn.config(text="翻转 ✓" if self._flip else "翻转")

    def _on_resolution_change(self, event):
        val = self._res_var.get()
        w, h = val.split('x')
        self._width, self._height = int(w), int(h)
        if self._vcam and self._vcam.is_open:
            self._vcam.close()
            self._vcam = VirtualCamera(width=self._width, height=self._height, fps=self._fps)
            self._vcam.open()

    def _quit(self):
        """退出"""
        if self._receiver:
            self._receiver.stop()
        if self._vcam:
            self._vcam.close()
        if self._manager:
            self._manager.stop()
        self.root.destroy()

    def run(self):
        """启动 GUI"""
        self.root.protocol("WM_DELETE_WINDOW", self._quit)
        self.root.mainloop()


def main():
    logging.basicConfig(level=logging.INFO)
    gui = PhoneCamGUI()
    gui.run()


if __name__ == "__main__":
    main()