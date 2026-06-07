import pyvirtualcam
print(f"pyvirtualcam version: {pyvirtualcam.__version__}")

try:
    cam = pyvirtualcam.Camera(width=640, height=480, fps=30)
    print(f"Device: {cam.device}")
    cam.close()
    print("OK!")
except Exception as e:
    print(f"Error: {e}")