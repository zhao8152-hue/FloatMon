from PIL import Image
import struct, io

# 读取原图
img = Image.open('G:/下载/Black_hole_icon_logo_202606021347.jpeg').convert('RGBA')

# 抠图：白色背景变透明
data = img.getdata()
new_data = []
for r, g, b, a in data:
    # 白色背景（容差）变透明
    if r > 240 and g > 240 and b > 240:
        new_data.append((255, 255, 255, 0))
    else:
        new_data.append((r, g, b, 255))
img.putdata(new_data)

# 裁切透明边距
bbox = img.getbbox()
img = img.crop(bbox)

# 做成正方形（居中）
w, h = img.size
size = max(w, h)
square = Image.new('RGBA', (size, size), (0, 0, 0, 0))
square.paste(img, ((size - w) // 2, (size - h) // 2))

# 生成多尺寸
sizes = [16, 24, 32, 48, 64, 128, 256]
pngs = []
for s in sizes:
    resized = square.resize((s, s), Image.Resampling.LANCZOS)
    pngs.append(resized)

# 打包 ICO
with open('icon.ico', 'wb') as f:
    f.write(struct.pack('<HHH', 0, 1, len(sizes)))
    offset = 6 + 16 * len(sizes)
    for s, img_s in zip(sizes, pngs):
        buf = io.BytesIO()
        img_s.save(buf, 'PNG')
        data = buf.getvalue()
        f.write(struct.pack('<BBBBHHII', s if s < 256 else 0, s if s < 256 else 0, 0, 0, 1, 32, len(data), offset))
        offset += len(data)
    for img_s in pngs:
        buf = io.BytesIO()
        img_s.save(buf, 'PNG')
        f.write(buf.getvalue())

print('icon.ico created with', len(sizes), 'sizes')
