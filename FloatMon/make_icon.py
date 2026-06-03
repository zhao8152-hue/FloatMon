""" 图标转换工具：任意 PNG/JPEG → 标准 Windows .ico 文件。
用法：python make_icon.py <输入图片路径>
输出：同目录下生成 icon.ico，含 16/24/32/48/64/128/256 七种尺寸。"""
import sys, os, struct, io
from PIL import Image

def main():
    if len(sys.argv) < 2:
        print("用法: python make_icon.py <图片路径>")
        print("示例: python make_icon.py logo.png")
        sys.exit(1)

    in_path = sys.argv[1]
    if not os.path.exists(in_path):
        print(f"错误: 文件不存在 {in_path}")
        sys.exit(1)

    # 读取原图，白色背景自动变透明
    img = Image.open(in_path).convert('RGBA')
    pixels = list(img.getdata())
    for i, (r, g, b, a) in enumerate(pixels):
        if r > 240 and g > 240 and b > 240:
            pixels[i] = (255, 255, 255, 0)
    img.putdata(pixels)

    # 裁边 + 正方形居中
    bbox = img.getbbox()
    if bbox:
        img = img.crop(bbox)
    w, h = img.size
    size = max(w, h)
    square = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    square.paste(img, ((size - w) // 2, (size - h) // 2))

    # 多尺寸缩放
    sizes = [16, 24, 32, 48, 64, 128, 256]
    pngs = [square.resize((s, s), Image.Resampling.LANCZOS) for s in sizes]

    # 打包 ICO
    out_dir = os.path.dirname(os.path.abspath(in_path))
    out_path = os.path.join(out_dir, 'icon.ico')
    with open(out_path, 'wb') as f:
        f.write(struct.pack('<HHH', 0, 1, len(sizes)))
        offset = 6 + 16 * len(sizes)
        for s, img_s in zip(sizes, pngs):
            buf = io.BytesIO()
            img_s.save(buf, 'PNG')
            data = buf.getvalue()
            f.write(struct.pack('<BBBBHHII',
                s if s < 256 else 0, s if s < 256 else 0,
                0, 0, 1, 32, len(data), offset))
            offset += len(data)
        for img_s in pngs:
            buf = io.BytesIO()
            img_s.save(buf, 'PNG')
            f.write(buf.getvalue())

    print(f'完成: {out_path} ({len(sizes)} 种尺寸)')

if __name__ == '__main__':
    main()
