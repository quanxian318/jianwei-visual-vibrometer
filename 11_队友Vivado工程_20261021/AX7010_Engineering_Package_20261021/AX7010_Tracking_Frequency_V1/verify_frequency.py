import json,time
from pathlib import Path
import numpy as np
from network_client import capture
from read_frequency import parse_frequency,KEYS
from read_tracking import parse
ROOT=Path(__file__).parent

def verify():
    for attempt in range(16):
        data,_=capture(9);f=parse_frequency(data)
        raw,_=capture(5);rows,stats=parse(raw)
        end=f['track_end_sequence'];selected=[r for r in rows if end-255<=r['sequence']<=end]
        if len(selected)==256 and any(s['valid'] for s in f['channels'].values()):break
        time.sleep(1)
    else:raise RuntimeError('No complete window shared by board FFT and tracking ring')
    result=dict(board={k:v for k,v in f.items() if k!='channels'},tracking=stats,comparison={})
    for ch,key in enumerate(KEYS):
        s=f['channels'][key]
        if not s['valid']:
            result['comparison'][key]={'skipped':s['reason']};continue
        bit=1<<(ch//2)
        assert all(r['flags']&r['reference_flags']&bit for r in selected)
        assert all(b['sequence']==a['sequence']+1 for a,b in zip(selected,selected[1:]))
        v=np.array([r[key] for r in selected]);t=np.arange(256)-127.5
        v-=np.polyval(np.polyfit(t,v,1),t)
        w=np.hanning(256);a=np.abs(np.fft.rfft(v*w))*2/w.sum();a[[0,-1]]*=.5
        error=float(np.max(np.abs(a-np.array(s['amplitude_px']))))
        assert error<.0001, (key,error)
        result['comparison'][key]={'maximum_absolute_spectral_difference_px':error,'passed':True}
    assert any('passed' in x for x in result['comparison'].values()),'No valid spectrum tested'
    assert stats['board_sample_fps']>=43,'Tracking rate regression'
    assert f['maximum_fft_ms']<5,'FFT exceeds available slack'
    result['passed']=True
    (ROOT/'verification.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    (ROOT/'verified_frequency.bin').write_bytes(data)
    (ROOT/'verified_tracking.bin').write_bytes(raw)
    print(json.dumps(result,indent=2))
if __name__=='__main__':verify()
