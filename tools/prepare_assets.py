"""Build Ikaros assets; no registry or desktop changes. Python is build-only."""
from pathlib import Path
import hashlib
import json
import _cursor_codec as codec

root = Path(__file__).resolve().parents[1]
source = Path(r"C:\WINDOWS\Cursors\Ikaros")
out = root / "resources"
manifest = []
for path in sorted(source.iterdir()):
    if path.suffix.lower() not in (".ani", ".cur"):
        continue
    raw = path.read_bytes()
    dest = out / "original" / path.name
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(raw)
    transform = codec.transform_ani if path.suffix.lower() == ".ani" else codec.transform_cur
    for name, opacity in (("fade50", 0.5), ("fade75", 0.25), ("fade90", 0.1), ("hidden", 0.0)):
        data, original = transform(raw, (opacity, False))
        _, rewritten = transform(data)
        if path.suffix.lower() == ".ani":
            assert original["anih"] == rewritten["anih"]
            assert original["sequence"] == rewritten["sequence"]
            assert original["rate_jiffies"] == rewritten["rate_jiffies"]
        dest = out / name / path.name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    manifest.append({"file": path.name, "sha256": hashlib.sha256(raw).hexdigest()})
(out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf8")
from PIL import Image, ImageDraw
icon = Image.new("RGBA", (64, 64))
d = ImageDraw.Draw(icon)
d.rounded_rectangle((3, 3, 60, 60), 15, fill="#173C46")
d.polygon([(17, 13), (17, 47), (26, 36), (35, 49), (41, 45), (31, 33), (46, 32)], fill="#A4E5D5")
d.ellipse((42, 12, 52, 22), fill="#F2C875")
icon.save(root / "Hide.ico", sizes=[(16,16),(24,24),(32,32),(48,48),(64,64)])
print(f"Prepared {len(manifest)} original resources and four opacity variants.")
