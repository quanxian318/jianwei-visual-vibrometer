import struct
import numpy as np

KEYS=['dx0','dy0','dx1','dy1']
REASONS=['valid','warming up','target/reference lost','missing or non-monotonic samples','irregular timestamps']
def parse_frequency(data):
    if len(data)!=8192:raise ValueError('Wrong frequency mailbox size')
    h=struct.unpack_from('<64I',data)
    if h[0]!=0x46524631 or h[1] or h[2]!=0x50415353 or h[3]&1:
        raise ValueError('Frequency worker uninitialized/unhealthy or snapshot inconsistent')
    v=np.frombuffer(data,dtype='<f4',offset=256,count=24+8*129)
    if not np.isfinite(v).all():raise ValueError('Nonfinite board spectrum')
    fields=v[:24].reshape(6,4);amp=v[24:24+4*129].reshape(4,129);noise=v[24+4*129:].reshape(4,129)
    channels={}
    for i,key in enumerate(KEYS):
        fs=float(fields[0,i]);valid=bool(h[5]&(1<<i));base=h[14]
        reason=REASONS[h[6+i]] if h[6+i]<len(REASONS) else 'unknown rejection'
        candidate=float(fields[5,i]) if valid and base==2 and fields[5,i]>0 else None
        if valid:
            reason=('无可靠候选谱峰' if candidate is None else '超过静止谱包络3倍，仅候选，未排除混叠') if base==2 else '请先记录有效静止基线'
        channels[key]=dict(valid=valid,reason=reason,candidate_hz=candidate,fs_hz=fs,
                           window_s=256/fs if fs>0 else None,bin_spacing_hz=fs/256,
                           detrended_rms_px=float(fields[1,i]),spectral_peak_hz=float(fields[2,i]),
                           spectral_peak_amplitude_px=float(fields[3,i]),baseline_peak_threshold_px=float(fields[4,i]),
                           frequency_hz=(np.arange(129)*fs/256).tolist(),amplitude_px=amp[i].tolist(),
                           baseline_max_amplitude_px=noise[i].tolist(),baseline_windows=h[16+i],baseline_motion_rejections=h[40+i])
    return dict(processing='AX7010 Cortex-A9 CPU1 FFT; PC decodes and plots only',
                selftest='passed',track_end_sequence=h[4],lock_sequence=h[3],updates=h[20],
                board_time_s=((h[22]<<32)|h[21])/h[12],baseline_state=h[14],
                baseline_elapsed_s=h[15]/1000,command_ack=h[32],
                command_token=h[30],fft_ms=h[10]*1000/h[12],maximum_fft_ms=h[11]*1000/h[12],channels=channels)
