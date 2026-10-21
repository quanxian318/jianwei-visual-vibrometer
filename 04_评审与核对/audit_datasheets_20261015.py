"""Read-only manufacturer-document collection for the V1.10 design audit."""
from pathlib import Path
import concurrent.futures, hashlib, json, urllib.request, time
from pypdf import PdfReader

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '01_数据手册' / '厂家核验_2026-10-15'
OUT.mkdir(parents=True, exist_ok=True)
SOURCES = {
 'TPS5430':'https://www.ti.com/lit/ds/symlink/tps5430.pdf',
 'TLV1117LV':'https://www.ti.com/lit/ds/symlink/tlv1117lv.pdf',
 'TPS7A20':'https://www.ti.com/lit/ds/symlink/tps7a20.pdf',
 'INA226':'https://www.ti.com/lit/ds/symlink/ina226.pdf',
 'TLV3011B':'https://www.ti.com/lit/ds/symlink/tlv3011b.pdf',
 'TCA9555':'https://www.ti.com/lit/ds/symlink/tca9555.pdf',
 'UCC27517':'https://www.ti.com/lit/ds/symlink/ucc27517.pdf',
 'SN74LVC1G125':'https://www.ti.com/lit/ds/symlink/sn74lvc1g125.pdf',
 'SN74LVC2G125':'https://www.ti.com/lit/ds/symlink/sn74lvc2g125.pdf',
 'SN74LVC2G17':'https://www.ti.com/lit/ds/symlink/sn74lvc2g17.pdf',
 'SN74AHCT1G125':'https://www.ti.com/lit/ds/symlink/sn74ahct1g125.pdf',
 '74LVC244A':'https://assets.nexperia.com/documents/data-sheet/74LVC_LVCH244A.pdf',
 'AO4407A':'https://www.aosmd.com/sites/default/files/res/datasheets/AO4407A.pdf',
 'AOD4184A':'https://www.aosmd.com/sites/default/files/res/datasheets/AOD4184A.pdf',
 'AO3400A':'https://www.aosmd.com/sites/default/files/res/datasheets/AO3400A.pdf',
 'MAX98357A':'https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf',
 'DS3231':'https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf',
 'LSM6DSV':'https://www.st.com/resource/en/datasheet/lsm6dsv.pdf',
 'ADXL355':'https://www.analog.com/media/en/technical-documentation/data-sheets/adxl354_adxl355.pdf',
 'VL53L1X':'https://www.st.com/resource/en/datasheet/vl53l1x.pdf',
 'VL53L0X':'https://www.st.com/resource/en/datasheet/vl53l0x.pdf',
 'ESP32-S3':'https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf',
 'SLVA716A':'https://www.ti.com/lit/an/slva716/slva716.pdf',
 'MF-MSMF':'https://www.bourns.com/docs/product-datasheets/mf-msmf.pdf',
 'SRP1038A':'https://www.bourns.com/docs/product-datasheets/srp1038a.pdf',
 'JST-GH':'https://www.jst-mfg.com/product/pdf/eng/eGH.pdf',
 'JST-PH':'https://www.jst-mfg.com/product/pdf/eng/ePH.pdf',
 'PJ-002BH':'https://www.sameskydevices.com/product/resource/pj-002bh.pdf',
 'NKK-JW':'https://www.nkkswitches.com/pdf/JW.pdf',
 'SMBJ':'https://www.littelfuse.com/assetdocs/littelfuse-tvs-diode-smbj-datasheet?assetguid=066446d7-d742-4332-a50a-01ac92845c7d',
 'PN532':'https://www.nxp.com/docs/en/user-guide/141520.pdf',
 'SD-SPI':'https://www.sdcard.org/downloads/pls/pdf/?p=Part1_Physical_Layer_Simplified_Specification_Ver9.10.jpg&f=Part1_Physical_Layer_Simplified_Specification_Ver9.10.pdf&e=EN_SS9_1',
}

def fetch(item):
 name,url=item
 p=OUT/(name+'.pdf')
 try:
  if not p.exists():
   req=urllib.request.Request(url,headers={'User-Agent':'Mozilla/5.0'})
   with urllib.request.urlopen(req,timeout=28) as r:
    data=r.read()
   if b'%PDF-' not in data[:1024]:
    return {'name':name,'url':url,'status':'not_pdf','bytes':len(data)}
   p.write_bytes(data)
  data=p.read_bytes()
  pdf=PdfReader(str(p))
  pages=[f'\n--- PDF PAGE {i+1} ---\n'+(page.extract_text(extraction_mode='layout') or '') for i,page in enumerate(pdf.pages)]
  p.with_suffix('.txt').write_text('\n'.join(pages),encoding='utf-8')
  return {'name':name,'url':url,'status':'ok','pages':len(pages),'sha256':hashlib.sha256(data).hexdigest(),'local_path':str(p)}
 except Exception as e:
  return {'name':name,'url':url,'status':'error','error':str(e)[:200]}

if __name__=='__main__':
 with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
  rows=list(pool.map(fetch,SOURCES.items()))
 (OUT/'manifest.json').write_text(json.dumps({'checked_at':time.strftime('%Y-%m-%d %H:%M:%S'),'sources':rows},ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps([{k:r[k] for k in ('name','status','pages','error') if k in r} for r in rows],ensure_ascii=False))
