"""Read-only audit of the 129-component JLC schematic snapshot."""
from pathlib import Path
from collections import Counter
import csv
import hashlib
import json

root=Path(__file__).parent
project=Path('D:/codex/见微_AX7010视觉测振仪')
current=json.loads((root/'current_netlist.json').read_text(encoding='utf-8'))
prior=json.loads((root/'progress_netlist.json').read_text(encoding='utf-8'))
parts={c['props']['Designator']:c for c in current['components'].values()}
old={c['props']['Designator']:c for c in prior['components'].values()}
checks=[];covered=set()
def canonical(c):
    return {'part':c['props'].get('Manufacturer Part'),'value':c['props'].get('Value'),'pins':sorted((str(p['number']),p.get('name'),p.get('net')) for p in c['pinInfoMap'].values())}
def check(name,passed,refs,basis):
    checks.append({'name':name,'passed':bool(passed),'refs':refs,'basis':basis});covered.update(refs)
def pin(r,n):return parts[r]['pinInfoMap'][str(n)]['net']
def value(r):return parts[r]['props'].get('Value','').replace('Ω','')
def ends(r):return sorted(p['net'] for p in parts[r]['pinInfoMap'].values())
unchanged=[r for r in old if r in parts and canonical(parts[r])==canonical(old[r])]
check('前次110器件电气未变，复用其已记录核查',len(unchanged)==110,unchanged,'型号、数值、脚名/脚号与网络的规范化比较')
channels=[('红灯','Q304','R315','R316','ALM_R','ALM_R_N',2),('黄灯','Q305','R317','R318','ALM_Y','ALM_Y_N',3),('绿灯','Q308','R319','R320','ALM_G','ALM_G_N',4),('蜂鸣器','Q309','R321','R322','BUZZ','BZ_N',5),('风扇','Q310','R323','R324','FAN','FAN_N',6)]
for label,q,series,down,control,drain,j in channels:
    check(label+'MOS型号及G/S/D',parts[q]['props']['Manufacturer Part']=='AO3400A' and pin(q,2)=='GND' and pin(q,3)==drain and pin(q,1)!=control,[q],'AOS AO3400A厂家PDF1/2，1G/2S/3D；额定VDS30V、VGS±12V，2.5V栅压RDS(on)上限48mΩ')
    check(label+'100Ω串阻到Gate',value(series)=='100' and ends(series)==sorted([control,pin(q,1)]),[series],'当前方案§7.3与实际网表')
    check(label+'1kΩ下拉在串阻之前',value(down)=='1k' and ends(down)==sorted([control,'GND']),[down],'TCA9555输入态100µA下拉核算；无100Ω直流分压')
    check(label+'独立驱动对应J302针脚',pin('J302',j)==drain and drain not in ['12V_MAIN','12V_ALM','GND'],[q,'J302'],'实际端口及漏极网络')
for d,drain in [('D303','BZ_N'),('D304','FAN_N')]:
    check(d+'续流方向及型号',parts[d]['props']['Manufacturer Part']=='SS14' and pin(d,1)=='12V_ALM' and pin(d,2)==drain and parts[d]['pinInfoMap']['1']['name']=='K' and parts[d]['pinInfoMap']['2']['name']=='A',[d],'C83852厂家资料PDF2/3：带色带端为阴极，40V/1A；此项按库K/A逻辑，不等于PCB焊盘映射已验证')
check('F304型号及保护后12V_ALM',parts['F304']['props']['Manufacturer Part']=='MF-MSMF110/24X-2' and ends('F304')==sorted(['12V_MAIN','12V_ALM']) and pin('J302',1)=='12V_ALM',['F304','J302'],'Bourns MF-MSMF厂家PDF1：1.1A保持/2.2A跳闸/24V，23°C条件')
check('J302型号/六针针序',parts['J302']['props']['Manufacturer Part']=='DB128V-5.08-6P-GN-S' and [pin('J302',n) for n in range(1,7)]==['12V_ALM','ALM_R_N','ALM_Y_N','ALM_G_N','BZ_N','FAN_N'],['J302'],'当前方案及全工程网表；机械针序/PCB另核')
check('五路控制和漏极均独立',len({c[4] for c in channels})==5 and len({c[5] for c in channels})==5 and len({pin(c[1],1) for c in channels})==5,[c[1] for c in channels],'实际网表节点唯一性')
check('继电器分支未出现且风扇新位号正确',not ({'RLY301','D305','J303','Q311','R325','R326'} & set(parts)) and parts['Q310']['props']['Manufacturer Part']=='AO3400A',['Q310','R323','R324'],'用户已确认删除继电器及未绘位号重排')
check('位号无重复',len(parts)==len(current['components']),[],'实际网表')
with (project/'00_当前设计方案/主载板_BOM_V1.10.csv').open(encoding='utf-8-sig',newline='') as f:bom=list(csv.DictReader(f))
expected={r['位号'] for r in bom};actual=set(parts)
check('实际器件均有当前BOM位号',actual<=expected,[],'244条主载板BOM')
nets={};empty=[]
for ref,c in parts.items():
    for p in c['pinInfoMap'].values():
        if p.get('net'):nets.setdefault(p['net'],[]).append(ref+'.'+str(p['number']))
        else:empty.append(ref+'.'+str(p['number']))
allowed={'U201.2','U201.3','U201.5','U202.2','U202.3','U202.5','U203.2','U203.3','U203.5','U205.4','D204.2','J201.2','J201.3'}
check('13个有意空脚保持一致',set(empty)==allowed,[],'已核对NC/ENA/未用USB数据脚，无新增空脚')
incomplete_controls={r:[endpoint for endpoint in nets[r] if endpoint.split('.')[0].startswith('U')] for r in ['ALM_R','ALM_Y','ALM_G','BUZZ','FAN','PRN_PWR_EN','LIGHT_OK']}
progress=[]
for page in [f'{n:02}' for n in range(1,9)]:
    refs=[r['位号'] for r in bom if r['原理图页']==page]
    progress.append({'page':page,'planned':len(refs),'present':len(set(refs)&actual),'missing':sorted(set(refs)-actual)})
result={'readSummary':json.loads((root/'current_read_summary.json').read_text(encoding='utf-8')),'componentCount':len(parts),'pinCount':sum(len(c['pinInfoMap']) for c in parts.values()),'netCount':len(nets),'reusedPriorChecks':len(unchanged),'newComponentCount':len(actual-set(old)),'coveredComponentCount':len(covered),'uncovered':sorted(actual-covered),'checks':checks,'failed':[c for c in checks if not c['passed']],'progress':progress,'emptyPins':sorted(empty),'singlePinNets':{k:v for k,v in nets.items() if len(v)==1},'pendingU801Connections':incomplete_controls,'calculations':{'maxInputStateControl_V':100e-6*1000,'pulldownCurrentAt3V3_A':3.3/1000,'allFivePulldownCurrent_A':5*3.3/1000,'AO3400A_minGateDriveReference_V':2.5,'TCA9555_VOH_atVcc3V_IOH8mA_V':2.6,'AO3400A_loss_at0p1A_25C_W':0.1**2*0.048},'knownGaps':['实际PCB封装焊盘及极性未检查','第4–8页及前面板独立工程尚未读取到器件','打印动态/SOA、MLCC有效容量、Bank35时序仍待验证','组合声光报警灯型号、共极接法及电流尚未提供']}
(root/'current_check_result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
assert not result['failed'] and not result['uncovered']
print(json.dumps({k:v for k,v in result.items() if k not in ['checks','progress']},ensure_ascii=False,indent=2))
