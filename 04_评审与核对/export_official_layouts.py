"""Render source PDF pages and captioned crops. Does not modify EDA files."""
from pathlib import Path
import hashlib, html, json
import pypdfium2 as pdfium
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / '01_数据手册' / '厂家核验_2026-10-15'
OUT = ROOT / '01_数据手册' / '官方Layout截图'
OUT.mkdir(exist_ok=True)
# Crop boxes are page fractions; full source pages are always retained.
ROWS = [
 ('TPS5430',24,'实际PCB示例','U201/U202/U203，DDA-8','Figure 7-12；输入旁路、续流二极管和开关电流回路紧凑，FB远离SW。',None),
 ('TLV1117LV',15,'实际PCB示例','U204，SOT-223/DCY','Figure 8-4；散热片/大焊盘为VOUT，不可接地。',None),
 ('TPS7A20',31,'实际PCB示例','U205，DBV/SOT-23-5','Figure 7-7为本项目DBV封装；不要采用下一页DQN/WLCSP图。',None),
 ('INA226',31,'实际PCB示例','U101，VSSOP-10','Figure 8-4；分流电阻Kelvin引线独立取样，不从负载大电流铜皮中途取样。',None),
 ('TLV3011B',28,'实际PCB示例','U102，DBV/SOT-23-6','Figure 10-7；反馈与参考节点短而安静，远离Buck和灯带开关回路。',None),
 ('TCA9555',31,'实际PCB示例','U801，PW/TSSOP-24','Figure 43；去耦贴近VCC，保留连续地回路。',None),
 ('UCC27517',23,'实际PCB示例','U301/U302，DBV/SOT-23-5','Figure 29明确为UCC27517DBV；驱动器—MOSFET Gate—Source回路短，去耦贴近。',None),
 ('MAX98357A-TQFN-EVK',7,'原厂评估板PCB','U602，ETE/TQFN-16','同封装评估板：顶层丝印、元件面铜层及Layer 2；供电去耦与功率回流可参考。',None),
 ('MAX98357A-TQFN-EVK',8,'原厂评估板PCB','U602，ETE/TQFN-16','同封装评估板：Layer 3与焊接面；评估板是4层，不能直接当成本项目Gerber。',None),
 ('MAX98357A',33,'文字布局指南','U602，ETE/TQFN-16','Layout and Grounding；主手册下一页WLP布局不适用于本项目封装，未截入示例。',None),
 ('SN74LVC1G125',15,'通用布局示意','U402，DBV/SOT-23-5','原厂图含通用转角、TSSOP/WQFN及6脚SOT示意，不是本型号5脚封装的逐脚布线模板。',None),
 ('SN74LVC2G125',14,'通用连接/走线示意','U601，DCU/VSSOP-8','Figure 7是未用输入端接法，Figure 8是转角示意；不是本封装的完整PCB布局。',None),
 ('SN74LVC2G17',11,'通用连接示意','U501，DBV/SOT-23-6','Figure 7虽题为Layout Diagram，主体为通用端接示意；不是PCB铜层图。',None),
 ('SN74AHCT1G125',11,'通用连接示意','U502，DBV/SOT-23-5','Figure 8-3为未用输入端接法，不是PCB铜层图；封装焊盘另见本索引。',None),
 ('DS3231',18,'文字布局/装配指南','U802，SO-16W','Handling, PCB Layout, and Assembly；没有官方走线实例。关注封装下走线隔离与禁止超声清洗。',None),
 ('WS2812B-V6',5,'典型应用电路','前面板HL101–HL105','原厂仅给级联应用电路，未给PCB Layout；每颗附近保留本项目去耦电容。',None),
 ('SN74LVC1G125',27,'封装焊盘参考','U402，DBV/SOT-23-5','DBV0005A推荐焊盘；仅用于核对封装尺寸，不是外围电路布局。',None),
 ('SN74LVC2G125',25,'封装焊盘参考','U601，DCU/VSSOP-8','DCU0008A推荐焊盘；仅用于核对封装尺寸，不是外围电路布局。',None),
 ('SN74LVC2G17',31,'封装焊盘参考','U501，DBV/SOT-23-6','DBV0006A推荐焊盘；仅用于核对封装尺寸，不是外围电路布局。',None),
 ('SN74AHCT1G125',18,'封装焊盘参考','U502，DBV/SOT-23-5','DBV0005A推荐焊盘；仅用于核对封装尺寸，不是外围电路布局。',None),
]

CROPS = {
 ('TPS5430',24):(.08,.13,.93,.60),
 ('TLV1117LV',15):(.24,.34,.75,.65),
 ('TPS7A20',31):(.27,.73,.73,.92),
 ('INA226',31):(.08,.38,.93,.83),
 ('TLV3011B',28):(.10,.12,.90,.38),
 ('TCA9555',31):(.08,.43,.93,.93),
 ('UCC27517',23):(.07,.10,.94,.70),
 ('MAX98357A-TQFN-EVK',7):(.07,.17,.94,.94),
 ('MAX98357A-TQFN-EVK',8):(.07,.17,.94,.57),
}

def font(size):
    return ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',size)

def main():
    records=[]
    for name,page,kind,target,note,crop in ROWS:
        source=SRC/(name+'.pdf')
        doc=pdfium.PdfDocument(str(source))
        img=doc[page-1].render(scale=2.4).to_pil().convert('RGB')
        stem=f'{name}_PDFp{page:02}'
        img.save(OUT/(stem+'_整页.png'))
        crop=CROPS.get((name,page),crop)
        cropped=None
        if crop:
            cropped=stem+'_图示.png'
            box=tuple(round(v*(img.width if i%2==0 else img.height)) for i,v in enumerate(crop))
            img.crop(box).save(OUT/cropped)
        records.append(dict(name=name,page=page,kind=kind,target=target,note=note,
                            source='../厂家核验_2026-10-15/'+source.name,
                            sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                            full=stem+'_整页.png',crop=cropped,crop_fraction=crop))
        doc.close()
    (OUT/'截图清单.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    write_index(records)
    # Visual QA sheet: full source pages, in the same order as the index.
    w,h=520,780
    sheet=Image.new('RGB',(w*4,h*((len(records)+3)//4)),'#e8e8e8');d=ImageDraw.Draw(sheet)
    for i,r in enumerate(records):
        x=(i%4)*w;y=(i//4)*h
        im=Image.open(OUT/r['full']);im.thumbnail((500,700))
        sheet.paste(im,(x+(w-im.width)//2,y+65))
        d.text((x+10,y+5),r['name']+' p'+str(r['page']),font=font(20),fill='black')
        d.text((x+10,y+32),r['kind'],font=font(18),fill='black')
    sheet.save(OUT/'总览_用于核对.png')
    print('Rendered',len(records),'source pages')

def write_index(records):
    esc=html.escape
    cards=[]
    for r in records:
        preview=r.get('crop') or r['full']
        cards.append(f'<article><h2>{esc(r["name"])} · PDF第{r["page"]}页</h2><p class="tag">{esc(r["kind"])}</p><p><b>{esc(r["target"])}</b></p><p>{esc(r["note"])}</p><p><a href="{esc(r["source"])}#page={r["page"]}">打开原始PDF</a> · <a href="{esc(r["full"])}">整页原图</a></p><a href="{esc(preview)}"><img loading="lazy" src="{esc(preview)}"></a></article>')
    out='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>见微 V1.10 官方Layout参考</title><style>body{font:16px/1.7 system-ui,"Microsoft YaHei";margin:28px;background:#f4f6fa;color:#182331}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:22px}article{background:white;border:1px solid #ccd4df;border-radius:10px;padding:20px}h1{margin-bottom:8px}h2{font-size:20px;margin:0}.tag{color:#185a84;font-weight:bold}img{width:100%;height:auto;border:1px solid #ddd}a{color:#0668ae}header{max-width:1100px;margin-bottom:24px}</style><header><h1>见微 AX7010 V1.10 · 官方Layout参考</h1><p>来源为已下载的原厂手册和原厂TQFN评估板资料。页码统一为PDF页序；示例不替代本项目实际布局审查。通用示意、典型电路、文字指南均明确标注，不当作对应封装的完整PCB图。未做图片重绘。</p><p>优先看TPS5430、UCC27517、TPS7A20和MAX98357A TQFN评估板。更多IC及模块资料见旁边的《IC手册与Layout索引.md》。</p></header><main>'''+''.join(cards)+'</main></html>'
    (OUT/'index.html').write_text(out,encoding='utf-8')

if __name__=='__main__':main()
