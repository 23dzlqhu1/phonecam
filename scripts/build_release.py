#!/usr/bin/env python3
"""PhoneCam 打包脚本

打包电脑端为 PyInstaller 单文件 .exe，Flutter APK。
"""

import subprocess
import sys
import os
import shutil
from pathlib import Path


def build_desktop():
    """打包电脑端为 .exe"""
    print("=" * 50)
    print("📦 打包电脑端 (PyInstaller)")
    print("=" * 50)

    desktop_dir = Path(__file__).parent.parent / "desktop"
    dist_dir = desktop_dir / "dist"
    build_dir = desktop_dir / "build"

    # 清理旧构建
    for d in [dist_dir, build_dir]:
        if d.exists():
            shutil.rmtree(d)
            print(f"  清理: {d}")

    # PyInstaller 命令
    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--onefile",
        "--name", "PhoneCam",
        "--console",  # 保留控制台输出
        "--add-data", f"{desktop_dir / 'receiver.py'};.",
        "--add-data", f"{desktop_dir / 'virtual_camera.py'};.",
        "--add-data", f"{desktop_dir / 'discovery.py'};.",
        "--add-data", f"{desktop_dir / 'usb_handler.py'};.",
        "--add-data", f"{desktop_dir / 'connection_manager.py'};.",
        "--hidden-import", "cv2",
        "--hidden-import", "pyvirtualcam",
        "--hidden-import", "numpy",
        "--hidden-import", "PIL",
        str(desktop_dir / "phonecam.py"),
    ]

    print(f"\n  运行: {' '.join(cmd[:5])}...")
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=str(desktop_dir))

    if result.returncode != 0:
        print(f"\n❌ 打包失败:\n{result.stderr}")
        return False

    exe_path = dist_dir / "PhoneCam.exe"
    if exe_path.exists():
        size_mb = exe_path.stat().st_size / (1024 * 1024)
        print(f"\n✅ 电脑端打包成功: {exe_path}")
        print(f"   大小: {size_mb:.1f} MB")
        return True
    else:
        print("\n❌ 打包完成但未找到 .exe")
        return False


def build_phone():
    """打包手机端 APK"""
    print("\n" + "=" * 50)
    print("📱 打包手机端 (Flutter APK)")
    print("=" * 50)

    phone_dir = Path(__file__).parent.parent / "phone"

    # Flutter clean + build
    steps = [
        (["flutter", "clean"], "清理构建缓存"),
        (["flutter", "pub", "get"], "获取依赖"),
        (["flutter", "build", "apk", "--release"], "构建 APK"),
    ]

    for cmd, desc in steps:
        print(f"\n  {desc}...")
        result = subprocess.run(
            cmd, capture_output=True, text=True, cwd=str(phone_dir)
        )
        if result.returncode != 0:
            print(f"  ❌ {desc}失败:\n{result.stderr[:500]}")
            return False

    apk_path = phone_dir / "build" / "app" / "outputs" / "flutter-apk" / "app-release.apk"
    if apk_path.exists():
        size_mb = apk_path.stat().st_size / (1024 * 1024)
        print(f"\n✅ APK 打包成功: {apk_path}")
        print(f"   大小: {size_mb:.1f} MB")
        return True
    else:
        print("\n❌ 打包完成但未找到 APK")
        return False


def create_install_script():
    """创建 Windows 安装脚本"""
    print("\n" + "=" * 50)
    print("📝 生成安装脚本")
    print("=" * 50)

    scripts_dir = Path(__file__).parent
    install_bat = scripts_dir / "install.bat"

    content = """@echo off
chcp 65001 >nul
echo ================================
echo   PhoneCam 安装程序
echo ================================
echo.

REM 检查 Python
python --version >nul 2>&1
if errorlevel 1 (
    echo [错误] 未找到 Python，请先安装 Python 3.10+
    echo 下载: https://www.python.org/downloads/
    pause
    exit /b 1
)

REM 安装依赖
echo [1/3] 安装 Python 依赖...
pip install -r requirements.txt --quiet
if errorlevel 1 (
    echo [警告] 部分依赖安装失败，尝试继续...
)

REM 创建桌面快捷方式
echo [2/3] 创建桌面快捷方式...
set SCRIPT_DIR=%~dp0
set DESKTOP=%USERPROFILE%\\Desktop

echo @echo off > "%DESKTOP%\\PhoneCam.bat"
echo cd /d "%SCRIPT_DIR%.." >> "%DESKTOP%\\PhoneCam.bat"
echo python desktop\\phonecam.py --gui >> "%DESKTOP%\\PhoneCam.bat"

echo [3/3] 完成!
echo.
echo 桌面已创建快捷方式: PhoneCam.bat
echo 双击即可启动 GUI 模式
echo.
echo 或手动运行:
echo   python phonecam.py              # 自动发现
echo   python phonecam.py --gui        # GUI 模式
echo   python phonecam.py --preview    # CLI + 预览
echo.
pause
"""

    install_bat.write_text(content, encoding='utf-8')
    print(f"  ✅ {install_bat}")
    return True


def main():
    print("🚀 PhoneCam 打包工具")
    print(f"   Python: {sys.version}")
    print()

    results = {}

    # 根据参数选择打包目标
    if len(sys.argv) > 1:
        target = sys.argv[1]
    else:
        target = "all"

    if target in ("all", "desktop"):
        results["desktop"] = build_desktop()

    if target in ("all", "phone"):
        results["phone"] = build_phone()

    if target in ("all", "scripts"):
        results["scripts"] = create_install_script()

    # 总结
    print("\n" + "=" * 50)
    print("📊 打包结果")
    print("=" * 50)
    for name, ok in results.items():
        print(f"  {'✅' if ok else '❌'} {name}")

    if all(results.values()):
        print("\n🎉 全部打包成功!")
    else:
        print("\n⚠️ 部分打包失败，请检查错误信息")
        sys.exit(1)


if __name__ == "__main__":
    main()