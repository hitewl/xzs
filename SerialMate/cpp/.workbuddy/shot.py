import ctypes, sys, struct
from ctypes import wintypes as w

u = ctypes.windll.user32
g = ctypes.windll.gdi32
u.SetProcessDPIAware()

cls = sys.argv[1] if len(sys.argv) > 1 else "SerialMateWnd"
out = sys.argv[2] if len(sys.argv) > 2 else r"D:\AI\hun\temp\shot.bmp"

hwnd = u.FindWindowW(cls, None)
if not hwnd:
    print("window not found")
    sys.exit(1)
u.ShowWindow(hwnd, 9)
u.SetForegroundWindow(hwnd)
ctypes.windll.kernel32.Sleep(400)

r = w.RECT()
u.GetWindowRect(hwnd, ctypes.byref(r))
W, H = r.right - r.left, r.bottom - r.top
print("rect", r.left, r.top, W, H)

hdc = u.GetWindowDC(hwnd)
mdc = g.CreateCompatibleDC(hdc)
bmp = g.CreateCompatibleBitmap(hdc, W, H)
g.SelectObject(mdc, bmp)
# PrintWindow 优先（被遮挡也能抓）
if not u.PrintWindow(hwnd, mdc, 2):
    g.BitBlt(mdc, 0, 0, W, H, hdc, 0, 0, 0x00CC0020)

class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", w.DWORD), ("biWidth", w.LONG), ("biHeight", w.LONG),
                ("biPlanes", w.WORD), ("biBitCount", w.WORD), ("biCompression", w.DWORD),
                ("biSizeImage", w.DWORD), ("biXPelsPerMeter", w.LONG), ("biYPelsPerMeter", w.LONG),
                ("biClrUsed", w.DWORD), ("biClrImportant", w.DWORD)]

bi = BITMAPINFOHEADER()
bi.biSize = ctypes.sizeof(bi)
bi.biWidth = W
bi.biHeight = -H
bi.biPlanes = 1
bi.biBitCount = 32
bi.biCompression = 0
buf = ctypes.create_string_buffer(W * H * 4)
g.GetDIBits(mdc, bmp, 0, H, buf, ctypes.byref(bi), 0)

row = W * 4
hdr = struct.pack("<2sIHHI", b"BM", 14 + 40 + len(buf), 0, 0, 14 + 40)
info = struct.pack("<IiiHHIIiiII", 40, W, H, 1, 32, 0, len(buf), 0, 0, 0, 0)
data = bytearray()
for y in range(H - 1, -1, -1):
    line = bytearray(buf.raw[y * row:(y + 1) * row])
    for x in range(3, len(line), 4):
        line[x] = 255
    data += line
open(out, "wb").write(hdr + info + bytes(data))
print("saved", out)
