# mobile_app — 见微测振 Android APP

说明、接口与验证结果见 [15_手机APP_20261025](../../15_手机APP_20261025/README.md)。

- `web/`：界面（HTML/CSS/JS，无构建步骤）。桌面浏览器打开 `web/index.html` 可直接试用演示模式。
- `android/`：Java WebView 外壳（`MainActivity` 与 `VibmonNative` 桥：UDP 发现、振动、分享、保存到下载、状态栏配色）、清单与资源。
- `tools/build_apk.sh`：无需 Android SDK 的构建脚本，输出 `dist/JianweiVibro-<版本>.apk`。`make_icons.py` 生成图标，`make_fonts.py` 生成字体子集，`apk_pack.py` 负责对齐，`signer/ApkSign.java` 负责签名。
- `docs/`：512 px 商店图标等。
