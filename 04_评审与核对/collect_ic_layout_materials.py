"""Download missing manufacturer PDFs; reuse verified local sources. No EDA mutation."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from urllib.request import Request, urlopen
from urllib.parse import urlsplit
from pypdf import PdfReader
import hashlib, json, re, ssl, time, html

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '01_数据手册' / '厂家核验_2026-10-15'
UA = {'User-Agent': 'Mozilla/5.0', 'Accept': 'application/pdf,*/*'}

def get_bytes(url, attempts=3):
    last = None
    for _ in range(attempts):
        try:
            with urlopen(Request(url, headers=UA), timeout=25) as r:
                chunks = []
                while True:
                    block = r.read(65536)
                    if not block: break
                    chunks.append(block)
                data = b''.join(chunks)
                size = r.headers.get('Content-Length')
                if size and len(data) != int(size):
                    raise ValueError('incomplete response')
                return data
        except Exception as e:
            last = e
    raise last

def lcsc_mirror(partcode):
    """Extract a public, signed datasheet link without logging its temporary query."""
    landing = f'https://jlcpcb.com/partdetail/{partcode}'
    s = get_bytes(landing, 1).decode('utf-8', 'replace')
    chunks = [s]
    for m in re.finditer(r'self\.__next_f\.push\((\[.*?\])\)</script>', s, re.S):
        try:
            v = json.loads(m[1])
            if len(v) > 1 and isinstance(v[1], str): chunks.append(v[1])
        except Exception: pass
    for chunk in chunks:
        chunk = html.unescape(chunk).replace('\\u0026', '&')
        for url in re.findall(r'https://[^\s"<>\\]+\.pdf(?:\?[^\s"<>\\]+)?', chunk):
            if 'oss' in urlsplit(url).netloc and partcode in urlsplit(url).path:
                try:
                    b = get_bytes(url, 1)
                    if b.startswith(b'%PDF-'): return b, landing
                except Exception: pass
    raise ValueError('public manufacturer-PDF link not retrievable')

SOURCES = {
    '74LVC244A': ['https://assets.nexperia.com/documents/data-sheet/74LVC_LVCH244A.pdf?download=1',
                  'https://docs.rs-online.com/61c3/0900766b816ae41e.pdf'],
    'ADXL355': ['https://www.analog.com/media/en/technical-documentation/data-sheets/adxl354_adxl355.pdf'],
    'SHT4x': ['https://sensirion.com/resource/datasheet/sht4x'],
    'PN532': ['https://www.nxp.com/docs/en/nxp/data-sheets/PN532_C1.pdf',
              'https://raw.githubusercontent.com/Xinyuan-LilyGO/T-Embed-CC1101/master/hardware/PN532_C1.pdf'],
    'NKK-JW': ['https://www.nkkswitches.com/pdf/JW.pdf'],
    'MPA106D': ['https://www.te.com/commerce/DocumentDelivery/DDEController?Action=srchrtrv&DocFormat=pdf&DocLang=English&DocNm=2-1437567-9&DocType=Customer+Drawing&PartCntxt=2-1437567-8'],
}

def fetch(item):
    name, urls = item
    p = OUT / (name + '.pdf')
    origin = urls[0]
    try:
        if not p.exists():
            data = None
            for url in urls:
                try:
                    candidate = get_bytes(url, 2)
                    if not candidate.startswith(b'%PDF-'): continue
                    if name == '74LVC244A':
                        import io
                        check = PdfReader(io.BytesIO(candidate))
                        if 'nexperia' not in '\n'.join((x.extract_text() or '') for x in check.pages).lower():
                            continue
                    data, origin = candidate, url
                    break
                except Exception: pass
            if data is None: raise ValueError('PDF download failed')
            import io
            PdfReader(io.BytesIO(data))  # Validate before saving.
            p.write_bytes(data)
        data = p.read_bytes()
        reader = PdfReader(p)
        text = '\n'.join(f'\n--- PDF PAGE {i+1} ---\n'+(page.extract_text() or '') for i,page in enumerate(reader.pages))
        p.with_suffix('.txt').write_text(text, encoding='utf-8')
        return {'name':name, 'url':origin, 'status':'ok', 'pages':len(reader.pages),
                'sha256':hashlib.sha256(data).hexdigest(), 'local_path':str(p)}
    except Exception as e:
        return {'name':name, 'url':origin, 'status':'error', 'error':type(e).__name__+': '+str(e)[:100]}

if __name__ == '__main__':
    with ThreadPoolExecutor(max_workers=6) as pool:
        rows = list(pool.map(fetch, SOURCES.items()))
    p = OUT / 'manifest.json'
    obj = json.loads(p.read_text(encoding='utf-8'))
    old = {r['name']:r for r in obj['sources']}
    old.update({r['name']:r for r in rows if r['status']=='ok' or r['name'] not in old})
    obj['sources'] = list(old.values())
    obj['supplement_checked_at'] = time.strftime('%Y-%m-%d %H:%M:%S')
    p.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding='utf-8')
    (OUT / 'supplement_status.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps([{k:r[k] for k in ['name','status','pages','error'] if k in r} for r in rows],ensure_ascii=False),flush=True)
