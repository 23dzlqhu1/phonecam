#!/usr/bin/env python3
"""PhoneCam Desktop GUI - 简洁现代设计

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

from receiver import PcpReceiver, video_frame_to_bgr
from virtual_camera import VirtualCamera
from connection_manager import ConnectionManager, ConnectionState, ConnectionInfo

logger = logging.getLogger(__name__)

# 颜色主题
COLORS = {
    'bg': '#0a0a0f',
    'surface': '#111827',
    'border': '#1f2937',
    'text': '#e5e7eb',
    'text_secondary': '#9ca3af',
    'text_muted': '#6b7280',
    'primary': '#3b82f6',
    'danger': '#ef4444',
    'success': '#10b981',
}


class PhoneCamGUI:
    """PhoneCam 桌面 GUI"""

    def __init__(self):
        self.root = tk.Tk()
        self.root.title("PhoneCam")
        self.root.geometry("900x640")
        self.root.minsize(700, 500)
        self.root.configure(bg=COLORS['bg'])

        # 状态
        self._manager: Optional[ConnectionManager] = None
        self._receiver: Optional[PcpReceiver] = None
        self._vcam: Optional[VirtualCamera] = None
        self._is_connected = False
        self._stream_confirmed = False  # G-024: 首帧到达确认标志
        self._current_frame: Optional[np.ndarray] = None
        self._frame_lock = threading.Lock()
        self._last_gui_state = None

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

        # 配置样式
        style.configure('Main.TFrame', background=COLORS['bg'])
        style.configure('Surface.TFrame', background=COLORS['surface'])
        style.configure('Title.TLabel',
                        font=('Segoe UI', 14, 'bold'),
                        background=COLORS['surface'],
                        foreground=COLORS['text'])
        style.configure('Status.TLabel',
                        font=('Segoe UI', 11),
                        background=COLORS['surface'],
                        foreground=COLORS['text_secondary'])
        style.configure('Info.TLabel',
                        font=('Consolas', 10),
                        background=COLORS['surface'],
                        foreground=COLORS['text_muted'])
        style.configure('Primary.TButton',
                        font=('Segoe UI', 11, 'bold'))

        # ── 顶部状态栏 ──
        status_frame = tk.Frame(self.root, bg=COLORS['surface'], height=70)
        status_frame.pack(fill='x', padx=0, pady=0)
        status_frame.pack_propagate(False)

        # 状态指示器
        self._status_dot = tk.Canvas(status_frame, width=12, height=12,
                                     bg=COLORS['surface'], highlightthickness=0)
        self._status_dot.pack(side='left', padx=(20, 10))
        self._dot_id = self._status_dot.create_oval(2, 2, 10, 10, fill=COLORS['text_muted'])

        # 状态文字
        status_text_frame = tk.Frame(status_frame, bg=COLORS['surface'])
        status_text_frame.pack(side='left', fill='y', pady=12)

        self._status_title = ttk.Label(status_text_frame, text="正在搜索设备...",
                                       style='Title.TLabel')
        self._status_title.pack(anchor='w')

        self._status_detail = ttk.Label(status_text_frame, text="请确保手机 App 已启动推流",
                                        style='Status.TLabel')
        self._status_detail.pack(anchor='w')

        # 右侧控制区
        control_frame = tk.Frame(status_frame, bg=COLORS['surface'])
        control_frame.pack(side='right', padx=20)

        # 分辨率选择
        ttk.Label(control_frame, text="分辨率", style='Status.TLabel').pack(side='left')
        self._res_var = tk.StringVar(value="640x480")
        res_combo = ttk.Combobox(control_frame, textvariable=self._res_var,
                                 values=["320x240", "640x480", "1280x720"],
                                 state='readonly', width=10)
        res_combo.pack(side='left', padx=(8, 0))
        res_combo.bind('<<ComboboxSelected>>', self._on_resolution_change)

        # ── 预览区域 ──
        preview_container = tk.Frame(self.root, bg=COLORS['bg'])
        preview_container.pack(fill='both', expand=True, padx=16, pady=16)

        self._canvas = tk.Canvas(preview_container, bg=COLORS['bg'],
                                highlightthickness=0)
        self._canvas.pack(fill='both', expand=True)

        # 初始占位
        self._canvas.create_text(
            450, 280, text="等待连接...",
            font=('Segoe UI', 16), fill=COLORS['text_muted'], tags='placeholder'
        )

        # ── 底部控制栏 ──
        bottom_frame = tk.Frame(self.root, bg=COLORS['surface'], height=56)
        bottom_frame.pack(fill='x', padx=0, pady=0)
        bottom_frame.pack_propagate(False)

        # 信息标签
        self._info_label = ttk.Label(bottom_frame, text="", style='Info.TLabel')
        self._info_label.pack(side='left', padx=20)

        # 按钮区
        btn_frame = tk.Frame(bottom_frame, bg=COLORS['surface'])
        btn_frame.pack(side='right', padx=20)

        self._mirror_btn = tk.Button(btn_frame, text="镜像", width=6,
                                     command=self._toggle_mirror,
                                     bg=COLORS['border'], fg=COLORS['text'],
                                     relief='flat', font=('Segoe UI', 10))
        self._mirror_btn.pack(side='left', padx=4)

        self._flip_btn = tk.Button(btn_frame, text="翻转", width=6,
                                   command=self._toggle_flip,
                                   bg=COLORS['border'], fg=COLORS['text'],
                                   relief='flat', font=('Segoe UI', 10))
        self._flip_btn.pack(side='left', padx=4)

        self._rotate_btn = tk.Button(btn_frame, text="旋转 0°", width=8,
                                     command=self._toggle_rotation,
                                     bg=COLORS['border'], fg=COLORS['text'],
                                     relief='flat', font=('Segoe UI', 10))
        self._rotate_btn.pack(side='left', padx=4)

        self._quit_btn = tk.Button(btn_frame, text="退出", width=6,
                                   command=self._quit,
                                   bg=COLORS['danger'], fg='white',
                                   relief='flat', font=('Segoe UI', 10))
        self._quit_btn.pack(side='left', padx=4)

        # 翻转/镜像/旋转状态
        self._mirror = False
        self._flip = False
        self._rotation = 0
        self._preview_id = None  # Canvas image item ID（用于 itemconfig 避免闪烁）

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
        """连接状态变化回调"""
        self.root.after(0, self._update_status_ui, info)

    def _update_status_ui(self, info: ConnectionInfo):
        """在 UI 线程更新状态"""
        if info.state == ConnectionState.SEARCHING:
            self._status_dot.itemconfig(self._dot_id, fill=COLORS['text_muted'])
            self._status_title.config(text="正在搜索设备...")
            self._status_detail.config(text="请确保手机 App 已启动推流")
        elif info.state == ConnectionState.WAITING_FOR_PHONE:
            # G-024: ADB 已就绪但手机未推流，琥珀色圆点
            self._status_dot.itemconfig(self._dot_id, fill='#f59e0b')
            self._status_title.config(text="等待手机推流...")
            self._status_detail.config(text="ADB 已就绪，请在手机端点击「开始推流」")
            # 提前启动 receiver，它会自动重连直到手机端就绪
            if not self._is_connected:
                self._is_connected = True
                self._stream_confirmed = False
                self._start_receiver(info.url)
        elif info.state == ConnectionState.CONNECTED:
            self._status_dot.itemconfig(self._dot_id, fill=COLORS['success'])
            self._status_title.config(text=f"已连接 ({info.connection_type})")
            self._status_detail.config(text=info.url)

            if not self._is_connected:
                self._is_connected = True
                self._start_receiver(info.url)
        elif info.state == ConnectionState.DISCONNECTED:
            self._status_dot.itemconfig(self._dot_id, fill=COLORS['danger'])
            self._status_title.config(text="连接断开")
            self._status_detail.config(text="正在重连...")

    def _start_receiver(self, url: str):
        """启动视频接收"""
        if self._receiver:
            self._receiver.stop()

        from urllib.parse import urlparse
        parsed = urlparse(url)
        host = parsed.hostname or "127.0.0.1"

        self._receiver = PcpReceiver(host, port=9999)
        self._receiver.on_frame(self._on_frame_pcp)
        self._receiver.start()

    def _on_frame_pcp(self, frame):
        """PcpReceiver 的帧回调，参数是 VideoFrame"""
        bgr_frame = video_frame_to_bgr(frame)
        if bgr_frame is not None:
            # G-024: 首帧到达 → 确认连接
            if not self._stream_confirmed:
                self._stream_confirmed = True
                if self._manager:
                    self._manager.confirm_stream_active()

            # 根据手机端的自动旋转信息旋转帧，保证虚拟摄像头和预览都显示正确方向的图像
            if frame.rotation == 90:
                bgr_frame = cv2.rotate(bgr_frame, cv2.ROTATE_90_CLOCKWISE)
            elif frame.rotation == 180:
                bgr_frame = cv2.rotate(bgr_frame, cv2.ROTATE_180)
            elif frame.rotation == 270:
                bgr_frame = cv2.rotate(bgr_frame, cv2.ROTATE_90_COUNTERCLOCKWISE)
            
            self._on_frame(bgr_frame)

        # 启动虚拟摄像头
        if not self._vcam:
            self._vcam = VirtualCamera(
                width=self._width, height=self._height, fps=self._fps
            )
            if not self._vcam.open():
                logger.warning("虚拟摄像头打开失败")
                self._vcam = None

    def _on_frame(self, frame: np.ndarray):
        """收到新帧"""
        safe_frame = frame.copy()  # 深拷贝隔离网络线程
        with self._frame_lock:
            self._current_frame = safe_frame

        if self._vcam and self._vcam.is_open:
            self._vcam.send(safe_frame)

        self._frame_count += 1

    def _update_loop(self):
        """定时更新 UI"""
        # G-024: 主线程轮询连接管理器状态，避开 Tkinter 跨线程通信失效问题
        if self._manager:
            info = self._manager.info
            if info.state != self._last_gui_state:
                self._last_gui_state = info.state
                self._update_status_ui(info)

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

        self.root.after(33, self._update_loop)

    def _display_frame(self, frame: np.ndarray):
        """在 Canvas 上显示帧（使用 itemconfig 避免撕裂）"""
        try:
            if self._mirror:
                frame = cv2.flip(frame, 1)
            if self._flip:
                frame = cv2.flip(frame, 0)

            # 根据用户手动旋转设置旋转帧
            if self._rotation == 90:
                frame = cv2.rotate(frame, cv2.ROTATE_90_COUNTERCLOCKWISE)
            elif self._rotation == 180:
                frame = cv2.rotate(frame, cv2.ROTATE_180)
            elif self._rotation == 270:
                frame = cv2.rotate(frame, cv2.ROTATE_90_CLOCKWISE)

            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

            canvas_w = self._canvas.winfo_width()
            canvas_h = self._canvas.winfo_height()
            h, w = rgb.shape[:2]
            if canvas_w > 1 and canvas_h > 1:
                scale = min(canvas_w / w, canvas_h / h)
                new_w = int(w * scale)
                new_h = int(h * scale)
                rgb = cv2.resize(rgb, (new_w, new_h))

            img = Image.fromarray(rgb)
            photo = ImageTk.PhotoImage(image=img)

            if self._preview_id is None:
                # 首帧：创建 image item
                self._canvas.delete('placeholder')
                self._preview_id = self._canvas.create_image(
                    canvas_w // 2, canvas_h // 2,
                    image=photo, anchor='center'
                )
            else:
                # 后续帧：原地更新，避免 delete+create 闪烁
                self._canvas.itemconfig(self._preview_id, image=photo)
                self._canvas.coords(self._preview_id, canvas_w // 2, canvas_h // 2)

            self._canvas._photo = photo
        except Exception as e:
            logger.debug(f"显示帧失败: {e}")

    def _toggle_mirror(self):
        self._mirror = not self._mirror
        self._mirror_btn.config(
            bg=COLORS['primary'] if self._mirror else COLORS['border']
        )

    def _toggle_flip(self):
        self._flip = not self._flip
        self._flip_btn.config(
            bg=COLORS['primary'] if self._flip else COLORS['border']
        )

    def _toggle_rotation(self):
        """循环切换旋转角度: 0 → 90 → 180 → 270 → 0"""
        self._rotation = (self._rotation + 90) % 360
        self._rotate_btn.config(
            text=f"旋转 {self._rotation}°",
            bg=COLORS['primary'] if self._rotation != 0 else COLORS['border']
        )

    def _on_resolution_change(self, event):
        val = self._res_var.get()
        w, h = val.split('x')
        self._width, self._height = int(w), int(h)
        if self._vcam and self._vcam.is_open:
            self._vcam.close()
            self._vcam = VirtualCamera(width=self._width, height=self._height, fps=self._fps)
            self._vcam.open()

    def _quit(self):
        if self._receiver:
            self._receiver.stop()
        if self._vcam:
            self._vcam.close()
        if self._manager:
            self._manager.stop()
        self.root.destroy()

    def run(self):
        self.root.protocol("WM_DELETE_WINDOW", self._quit)
        self.root.mainloop()


def main():
    logging.basicConfig(level=logging.INFO)
    gui = PhoneCamGUI()
    gui.run()


if __name__ == "__main__":
    main()