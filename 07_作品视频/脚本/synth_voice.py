#!/usr/bin/env python3
"""用百度语音合成为作品视频配旁白：发音人度怀安（大模型音库 per=4195）。

旁白文本由 render_video.py 的字幕换算；文本和参数都没变的句子沿用已有音频。
密钥只从环境变量读取，不写入仓库：
    BAIDU_API_KEY=... BAIDU_SECRET_KEY=... python3 synth_voice.py
"""
import hashlib
import json
import os
import time
import urllib.parse
import urllib.request
import wave

import render_video as rv

PARAMS = {"per": 4195, "spd": 6, "pit": 5, "vol": 5, "aue": 6}


def token():
    q = urllib.parse.urlencode({"grant_type": "client_credentials", "client_id": os.environ["BAIDU_API_KEY"],
                                "client_secret": os.environ["BAIDU_SECRET_KEY"]})
    with urllib.request.urlopen("https://aip.baidubce.com/oauth/2.0/token?" + q, data=b"", timeout=20) as r:
        return json.load(r)["access_token"]


def synth(tok, text):
    data = urllib.parse.urlencode({"tex": urllib.parse.quote_plus(text), "tok": tok, "cuid": "jianwei-video",
                                   "ctp": 1, "lan": "zh", **PARAMS}).encode()
    for attempt in range(3):  # 网络偶发超时时重试
        try:
            with urllib.request.urlopen("https://tsn.baidu.com/text2audio", data=data, timeout=30) as r:
                body, kind = r.read(), r.headers.get("Content-Type", "")
            break
        except OSError:
            if attempt == 2:
                raise
            time.sleep(3 * (attempt + 1))
    if not kind.startswith("audio"):
        raise RuntimeError(f"{text}: {body.decode('utf-8', 'replace')}")
    return body


def main():
    rv.VOICE_DIR.mkdir(parents=True, exist_ok=True)
    tok, clips = None, []
    for text in dict.fromkeys(rv.narration_lines()):
        name = hashlib.sha1((text + json.dumps(PARAMS, sort_keys=True)).encode()).hexdigest()[:12] + ".wav"
        path = rv.VOICE_DIR / name
        if not path.exists():
            tok = tok or token()
            path.write_bytes(synth(tok, text))
            print("合成", text)
        with wave.open(str(path)) as w:
            clips.append({"text": text, "file": name, "duration": round(w.getnframes() / w.getframerate(), 3)})
    rv.VOICE_LOG.write_text(json.dumps({"voice": "度怀安", "params": PARAMS, "clips": clips}, ensure_ascii=False,
                                       indent=1) + "\n", encoding="utf-8")
    print(f"{len(clips)} 句，共 {sum(c['duration'] for c in clips):.1f} s")


if __name__ == "__main__":
    main()
