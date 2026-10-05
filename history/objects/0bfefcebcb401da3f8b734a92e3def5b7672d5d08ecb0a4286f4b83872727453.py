"""mio：把六张真实浏览器截图拼成选型对照，不改截图中的图标和控件。"""
from pathlib import Path
from PIL import Image

base = Path(__file__).resolve().parent
names = ("sandstone", "ocean", "paper", "aurora", "cockpit", "candy")
# 每个格子保留同样的宽高：候选的主界面和底部名称都能一起比较。
# 缩略图只负责展示布局，文字/交互仍应在完整的原型里查看。
images = [Image.open(base / (name + ".jpg")).convert("RGB") for name in names]
width = 512
height = max(round(image.height * width / image.width) for image in images)
sheet = Image.new("RGB", (width * 3, height * 2), images[0].getpixel((0, 0)))
for index, image in enumerate(images):
    thumb = image.resize((width, round(image.height * width / image.width)), Image.Resampling.LANCZOS)
    sheet.paste(thumb, ((index % 3) * width, (index // 3) * height))
sheet.save(base / "overview.png")
print("Six browser screenshots: " + str(base / "overview.png"))
