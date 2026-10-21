"""Check the new load-driver circuits in the JLC engineering netlist."""
from pathlib import Path
from collections import Counter
import csv
import json

ROOT = Path(__file__).parent
PROJECT = Path('D:/codex/见微_AX7010视觉测振仪')
current = json.loads((ROOT / 'progress_netlist.json').read_text(encoding='utf-8'))
old = json.loads((PROJECT / '04_评审与核对/2026-10-16_在绘原理图核对/最新网表快照.json').read_text(encoding='utf-8-sig'))
parts = {c['props']['Designator']: c for c in current['components'].values()}
old_parts = {c['props']['Designator']: c for c in old['components'].values()}
checks = []
covered = set()

def pin(ref, number):
    return parts[ref]['pinInfoMap'][str(number)]['net']

def check(name, condition, refs, basis):
    checks.append({'name': name, 'passed': bool(condition), 'refs': refs, 'basis': basis})
    covered.update(refs)

def device(ref, mpn, expected, basis):
    check(ref + '型号/物理引脚网络', parts[ref]['props']['Manufacturer Part'] == mpn and all(pin(ref, p) == net for p, net in expected.items()), [ref], basis)

def two(ref, a, b, value=None, min_voltage=None, basis='方案及网表标称参数'):
    actual = [p['net'] for p in parts[ref]['pinInfoMap'].values()]
    props = parts[ref]['props']
    good = sorted(actual) == sorted([a, b])
    if value is not None:
        good &= props.get('Value', '').replace('Ω', '').replace('µ', 'u') == value
    if min_voltage is not None:
        good &= float(props['Voltage Rating'].replace('V', '')) >= min_voltage
    check(ref + '两端网络/标称数值', good, [ref], basis)

def cap(ref, rail, value, volts, polarized=False):
    two(ref, rail, 'GND', value, volts)
    if polarized:
        check(ref + '库1正2负约定下的极性网络', pin(ref, 1) == rail and pin(ref, 2) == 'GND', [ref], '库极性约定；实际封装/丝印另核')

def canonical(comp):
    return {'part': comp['props'].get('Manufacturer Part'), 'value': comp['props'].get('Value'), 'pins': sorted([(str(p['number']), p.get('name'), p.get('net')) for p in comp['pinInfoMap'].values()])}

unchanged = [r for r in old_parts if r in parts and canonical(parts[r]) == canonical(old_parts[r])]
check('前次81器件电气快照保持不变', len(unchanged) == 81 and len(old_parts) == 81, unchanged, '前后MCP全工程网表电气差异；复用前次已记录核查')

device('U302', 'UCC27517DBVR', {1:'5V_AUX',2:'GND',3:pin('R305',2),4:'LED_EN',5:pin('R307',2)}, 'UCC27517 PDF4 Pin Functions')
device('Q307', 'AOD4184A', {1:pin('R307',1),2:pin('J304',2),3:'GND'}, 'AOD4184A PDF1/2')
device('D302', 'SK54B-LTP', {1:pin('J304',1),2:pin('Q307',2)}, 'MCC SK54B-L 数据表及低边钳位拓扑')
device('F302', 'MF-MSMF110/24X-2', {1:pin('J304',1),2:'12V_MAIN'}, 'Bourns MF-MSMF PDF1：1.1A/24V')
device('J304', 'JL128-50802G01', {1:pin('F302',1),2:pin('Q307',2)}, '补光输出针序')
two('R305','LED2_PWM',pin('U302',3),'100')
two('R306','GND',pin('U302',3),'2.2k')
two('R307',pin('U302',5),pin('Q307',1),'10')
two('R308','GND',pin('Q307',1),'100k')
cap('C303','5V_AUX','1uF',6)
cap('C304','5V_AUX','100nF',6)

device('J305','SM02B-GHS-TB(LF)(SN)',{1:'LED_EN',2:'GND',3:'GND',4:'GND'},'GH2P信号1/2，库中3/4固定焊盘接地；厂家机械焊盘尚未在PCB校验')
two('R309','LED_EN','5V_AUX','10k')
cap('C305','LED_EN','100nF',6)
device('Q306','2N7002',{1:'LED_EN',2:'GND',3:'LIGHT_OK'},'CJ 2N7002 PDF1/2，1G/2S/3D')
two('R310','LIGHT_OK','AX_3V3','10k')
check('两路补光漏极网络独立', pin('Q301',2) != pin('Q307',2), ['Q301','Q307'], '实际网表')
check('补光硬件允许为公共5V域', all(pin(r,p) == 'LED_EN' for r,p in [('U301',4),('U302',4),('Q306',1),('J305',1)]), ['U301','U302','Q306','J305'], 'UCC27517双输入逻辑与MOS开漏状态转换')

prn_source = pin('F303',2)
prn_gate = pin('Q302',4)
device('F303','MF-MSMF250/16X-2',{1:'12V_MAIN',2:prn_source},'Bourns MF-MSMF PDF1：2.5A/16V')
device('Q302','AO4407A',{1:prn_source,2:prn_source,3:prn_source,4:prn_gate,5:'12V_PRN',6:'12V_PRN',7:'12V_PRN',8:'12V_PRN'},'AOS AO4407A PDF1：1/2/3 S、4 G、5/6/7/8 D')
device('Q303','AO3400A',{1:pin('R313',1),2:'GND',3:pin('R312',2)},'AOS AO3400A PDF1/2：1G/2S/3D，2.5V栅压规格')
device('DZ301','BZT52C12-TP',{1:prn_source,2:prn_gate},'MCC BZT52C12数据表：12V、阴极标记；网表K1到Source/A2到Gate正确，实际PCB焊盘极性另核')
two('R311',prn_source,prn_gate,'100k')
two('R312',prn_gate,pin('Q303',3),'47k')
two('R313','PRN_PWR_EN',pin('Q303',1),'100')
two('R314','PRN_PWR_EN','GND','1k')
check('R314位于R313之前且未接到Gate侧', pin('R314',2) == 'PRN_PWR_EN' and pin('Q303',1) != 'PRN_PWR_EN', ['R314','R313','Q303'], 'TCA9555输入态默认下拉及避免100Ω直流分压的现行方案')
two('C309',prn_gate,'12V_PRN','100nF',35,basis='TI SLVA716A：Gate–Drain受控启动拓扑')
two('C310',prn_source,prn_gate,'2.2uF',25,basis='TI SLVA716A与现行方案：上电Gate跟随Source，实际有效容量另验')
cap('C306','12V_PRN','470uF',35,True)
cap('C307','12V_PRN','1uF',25)
cap('C308','12V_PRN','100nF',35)
check('打印输出未直接并入常电母线', '12V_PRN' != prn_source and prn_source != '12V_MAIN', ['Q302','F303'], '实际网表输出/保险丝后/母线网络不同')

nets={}
for ref,c in parts.items():
    for p in c['pinInfoMap'].values():
        if p.get('net'): nets.setdefault(p['net'],[]).append(ref+'.'+str(p['number']))
allowed = {'U201.2','U201.3','U201.5','U202.2','U202.3','U202.5','U203.2','U203.3','U203.5','U205.4','D204.2','J201.2','J201.3'}
empty={ref+'.'+str(p['number']) for ref,c in parts.items() for p in c['pinInfoMap'].values() if not p.get('net')}
check('未连接引脚仍只有13个已核对的有意悬空脚', empty == allowed, [], '厂家NC/ENA/USB电源专用定义')
check('器件位号无重复', len(parts) == len(current['components']), [], '网表统计')
with (PROJECT/'00_当前设计方案/主载板_BOM_V1.10.csv').open(encoding='utf-8-sig',newline='') as f: bom=list(csv.DictReader(f))
progress=[]
for page in [f'{x:02}' for x in range(1,9)]:
    expected=[r['位号'] for r in bom if r['原理图页']==page]
    present=[r for r in expected if r in parts]
    progress.append({'page':page,'present':len(present),'planned':len(expected),'missing':[r for r in expected if r not in parts]})

rg=100000.;rd=47000.;cgd=100e-9;cgs=2.2e-6;vin=12.;plateau=3.
vg=-vin*rg/(rg+rd); ig=(vin-plateau)/rd-plateau/rg; rise=cgd*vin/ig
calculations={'printerVgs_V':vg,'nominalMillerRise_s':rise,'capacitor470uFCurrent_A':470e-6*vin/rise,'prePlateauRC_s':(rg*rd/(rg+rd))*(cgd+cgs),'gateTurnOffRC_s':rg*cgs,'led1_W':9,'led2_W':2,'totalLight_W':11,'totalLight_A':11/12,'captureEstimate_W':28.7,'printAfterLightsOff_2A_W':41.7,'printAfterLightsOff_3A_W':53.7}
result={'componentCount':len(parts),'pinCount':sum(len(x['pinInfoMap']) for x in parts.values()),'netCount':len(nets),'reusedPriorComponentChecks':len(unchanged),'newComponentCount':len(parts)-len(unchanged),'coveredComponentCount':len(covered),'uncovered':sorted(set(parts)-covered),'checks':checks,'failed':[x for x in checks if not x['passed']],'missingByPage':progress,'singlePinNets':{k:v for k,v in nets.items() if len(v)==1},'emptyPins':sorted(empty),'calculations':calculations}
(ROOT/'progress_check_result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ('checks',)},ensure_ascii=False))
