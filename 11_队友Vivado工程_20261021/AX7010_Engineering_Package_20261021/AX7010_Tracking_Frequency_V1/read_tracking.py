import csv,json,struct,statistics
from pathlib import Path
import numpy as np
from network_client import save
ROOT=Path(__file__).parent
def parse(data):
    header=struct.unpack_from('<16I',data)
    if header[0]!=0x54524b32 or header[1] or header[2]!=0x50415353:raise ValueError('Tracker not healthy')
    reference_seed=struct.unpack_from('<4I',data,160)
    end=header[3];hz=header[8];records=[]
    for seq in range(max(1,end-511),end+1):
        v=struct.unpack_from('<4I6f6I6f2I',data,256+(seq%512)*96)
        if v[0]!=seq:continue
        records.append(dict(sequence=seq,time_s=((v[2]<<32)|v[1])/hz,
          buffer0=v[3]&255,buffer1=(v[3]>>8)&255,x0=v[4],y0=v[5],score0=v[6],
          x1=v[7],y1=v[8],score1=v[9],flags=v[10],copy_ms=v[11]*1000/hz,
          track_ms=v[12]*1000/hz,lost0=v[13],lost1=v[14],
          refx0=v[16],refy0=v[17],refscore0=v[18],refx1=v[19],refy1=v[20],refscore1=v[21],reference_flags=v[22]))
        for ch in (0,1):
            records[-1][f'dx{ch}']=v[4+ch*3]-header[9+ch*2]-(v[16+ch*3]-reference_seed[ch*2])
            records[-1][f'dy{ch}']=v[5+ch*3]-header[10+ch*2]-(v[17+ch*3]-reference_seed[ch*2+1])
    stats={'records':len(records),'board_sample_fps':header[4]/10,'copy_rejections_total':header[7],
      'self_test':'passed','coordinate_units':'native sensor pixels, NOT mm or microns',
      'timestamps':'CPU observation/copy-start times, NOT verified sensor exposure synchronization',
      'seed_left':[header[9],header[10]],'seed_right':[header[11],header[12]]}
    stats['reference_seed']=list(reference_seed)
    if len(records)>1:
        dt=np.diff([r['time_s'] for r in records]);stats.update(time_span_s=records[-1]['time_s']-records[0]['time_s'],
           observed_rate_hz=(len(records)-1)/(records[-1]['time_s']-records[0]['time_s']),
           interval_min_ms=float(dt.min()*1000),interval_max_ms=float(dt.max()*1000),interval_std_ms=float(dt.std()*1000))
    for ch in (0,1):
        good=[r for r in records if r['flags']&(1<<ch)]
        stats[f'camera{ch+1}_valid_fraction']=len(good)/len(records) if records else 0
        if good:
            for axis in ('x','y'):
                t=np.array([r['time_s'] for r in good]);v=np.array([r[f'{axis}{ch}'] for r in good]);t-=t.mean()
                trend=np.polyval(np.polyfit(t,v,1),t) if len(good)>2 else np.full_like(v,v.mean())
                stats[f'camera{ch+1}_{axis}_std_px']=float(v.std())
                stats[f'camera{ch+1}_{axis}_detrended_rms_px']=float(np.std(v-trend))
            stats[f'camera{ch+1}_buffer_means']={str(b):{
                axis:float(np.mean([r[f'{axis}{ch}'] for r in good if r[f'buffer{ch}']==b]))
                for axis in ('x','y')} for b in (0,1,2) if any(r[f'buffer{ch}']==b for r in good)}
        paired=[r for r in good if r['reference_flags']&(1<<ch)]
        stats[f'camera{ch+1}_reference_valid_fraction']=len(paired)/len(records) if records else 0
        if paired:
            for axis in ('x','y'):
                v=np.array([r[f'd{axis}{ch}'] for r in paired]);t=np.array([r['time_s'] for r in paired]);t-=t.mean()
                fit=np.polyval(np.polyfit(t,v,1),t) if len(v)>2 else np.full_like(v,v.mean())
                stats[f'camera{ch+1}_relative_{axis}_std_px']=float(v.std())
                stats[f'camera{ch+1}_relative_{axis}_detrended_rms_px']=float(np.std(v-fit))
    return records,stats
if __name__=='__main__':
    data,_=save(5);records,stats=parse(data)
    if records:
        with (ROOT/'tracking_samples.csv').open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(records[0]));w.writeheader();w.writerows(records)
    (ROOT/'tracking_statistics.json').write_text(json.dumps(stats,indent=2))
    print(json.dumps(stats,indent=2))
