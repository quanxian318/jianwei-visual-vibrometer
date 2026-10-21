"""Plot spectra computed by AX7010. Only finite, explicit baseline controls."""
import io,json,time,secrets,threading,urllib.request
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from network_client import capture
from read_frequency import parse_frequency,KEYS
ROOT=Path(__file__).parent
state={'error':'Waiting for board FFT','channels':{}}
network_lock=threading.Lock()

def update():
    global state
    previous=None;changed=time.monotonic()
    while True:
        try:
            with network_lock:data,meta=capture(9)
            s=parse_frequency(data)
            if s['updates']!=previous:changed=time.monotonic();previous=s['updates']
            if time.monotonic()-changed>4:raise RuntimeError('板上频谱停止更新或尚未积累足够样本')
            s.update(error='',received_at=meta['received_at'])
            try:
                with urllib.request.urlopen('http://127.0.0.1:8767/status',timeout=2) as r:tracking=json.load(r)
                st=tracking['statistics']
                s['tracking_quality']={k:st.get(k) for k in ('board_sample_fps','camera1_valid_fraction','camera2_valid_fraction','camera1_reference_valid_fraction','camera2_reference_valid_fraction')}
                s['tracking_quality']['data_age_s']=time.time()-tracking['received_at']
                s['tracking_quality']['error']=tracking['error']
            except Exception as e:s['tracking_quality']={'error':str(e)}
            state=s
        except Exception as e:
            state={'error':str(e),'channels':{}}
        time.sleep(.7)

PAGE='''<!doctype html><meta charset="utf-8"><title>AX7010 板上频谱</title>
<style>body{background:#151b25;color:#ecf0f6;font:16px sans-serif;margin:24px;max-width:1100px}p{line-height:1.6}.warn{color:#ffcd80}canvas{background:#202a39;width:100%;height:170px}img{width:640px;max-width:100%}pre{white-space:pre-wrap}a{color:#8df}button{padding:12px;font-size:16px;cursor:pointer}</style>
<h2>双目标记 · 板上频谱测试 V1</h2><p class="warn">跟踪与FFT均在AX7010的ARM处理器运行，本页仅解码与绘图；FFT尚未使用PL硬件加速。位移单位为像素，未标定三维或微米。仅分析0.5～18 Hz候选谱峰。约45Hz采样不能排除高频混叠，两路一致也不能排除。</p>
<p>保持摄像头、目标和背景参考静止，然后点按钮记录基线：至少30秒；若有效窗口不足，延长到最多60秒。不降低门限，不填补丢失数据。基线不跨重启保存。更换光照、参考点或场景后需要重新记录。紫色小框是背景参考，绿色十字是目标；参考框变红表示无效。门限不是已标定的检出精度。</p>
<button id="baseline">确认静止，记录30秒板上基线</button> <span id="command"></span>
<p><a href="http://127.0.0.1:8767/">跟踪数据与坐标下载</a> · <a href="/snapshot.json">下载当前板上频谱</a></p><p id="health"></p><img id="screen" src="http://127.0.0.1:8767/image">
<p id="quality"></p><div id="graphs"></div><pre id="summary"></pre><script>
const keys=['dx0','dy0','dx1','dy1'],names=['左路水平','左路纵向','右路水平','右路纵向'];
document.querySelector('#graphs').innerHTML=keys.map((k,i)=>'<h3>'+names[i]+' · 像素幅度谱</h3><p id="label'+k+'"></p><canvas width="1000" height="180" id="c'+k+'"></canvas>').join('');
function plot(k,s){let g=document.querySelector('#c'+k).getContext('2d');g.clearRect(0,0,1000,180);if(!s||!s.valid)return;let a=s.amplitude_px,f=s.frequency_hz,b=s.baseline_max_amplitude_px,m=Math.max(.00001,...a.filter((_,i)=>f[i]>=.5&&f[i]<=18),...b.filter((_,i)=>f[i]>=.5&&f[i]<=18));for(let [v,col] of [[b,'#778899'],[a,'#63dfa1']]){g.strokeStyle=col;g.beginPath();let first=true;for(let i=0;i<f.length;i++){if(f[i]<.5||f[i]>18)continue;let x=45+f[i]/18*930,y=150-v[i]/m*125;if(first){g.moveTo(x,y);first=false}else g.lineTo(x,y)}g.stroke()}g.fillStyle='#ddd';g.fillText('0 Hz',40,174);g.fillText('18 Hz',940,174);g.fillText(m.toPrecision(3)+' px',5,15);}
async function refresh(){try{let s=await(await fetch('/status')).json(),age=s.received_at?Date.now()/1000-s.received_at:Infinity;document.querySelector('#health').textContent=s.error?'数据不可用：'+s.error:'数据年龄 '+age.toFixed(1)+' 秒；基线状态 '+['未采集','采集中','就绪','有效窗口不足，请检查参考点后重采'][s.baseline_state]+' '+s.baseline_elapsed_s+'秒；绿色当前谱，灰色基线谱';document.querySelector('#baseline').disabled=s.baseline_state===1;let tq=s.tracking_quality,pct=v=>v===undefined?'未知':(v*100).toFixed(1)+'%';document.querySelector('#quality').textContent=tq&&!tq.error&&tq.data_age_s<3?'最近约11秒：左目标 '+pct(tq.camera1_valid_fraction)+'，左参考 '+pct(tq.camera1_reference_valid_fraction)+'；右目标 '+pct(tq.camera2_valid_fraction)+'，右参考 '+pct(tq.camera2_reference_valid_fraction):'跟踪质量信息不可用或已过期';for(let k of keys){let q=s.channels?.[k];plot(k,q);document.querySelector('#label'+k).textContent=q?(q.candidate_hz!==null?'候选 '+q.candidate_hz.toFixed(2)+' Hz；':'')+q.reason:'无有效数据'}document.querySelector('#summary').textContent=JSON.stringify({processing:s.processing,selftest:s.selftest,fft_ms:s.fft_ms,maximum_fft_ms:s.maximum_fft_ms,updates:s.updates,channels:Object.fromEntries(Object.entries(s.channels||{}).map(([k,v])=>[k,{valid:v.valid,rms_px:v.detrended_rms_px,bin_spacing_hz:v.bin_spacing_hz,window_s:v.window_s,baseline_windows:v.baseline_windows,baseline_motion_rejections:v.baseline_motion_rejections}]))},null,2);document.querySelector('#screen').src='http://127.0.0.1:8767/image?t='+Date.now()}catch(e){document.querySelector('#health').textContent=e}setTimeout(refresh,1000)}refresh();
document.querySelector('#baseline').onclick=async()=>{if(!confirm('确认摄像头、目标与背景参考都已固定，并保持静止至少30秒？'))return;try{let r=await fetch('/baseline',{method:'POST',headers:{'Content-Type':'application/json'},body:'{"stationary_confirmed":true}'});document.querySelector('#command').textContent=await r.text()}catch(e){document.querySelector('#command').textContent=e}};
</script>'''
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path=='/':data=PAGE.encode();mime='text/html; charset=utf-8'
        elif self.path in ('/status','/snapshot.json'):data=json.dumps(state,allow_nan=False).encode();mime='application/json'
        else:self.send_error(404);return
        self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data)
    def do_POST(self):
        if self.path!='/baseline':self.send_error(404);return
        if self.headers.get('Origin') not in (None,'http://127.0.0.1:8769') or self.headers.get('Content-Type')!='application/json':self.send_error(403);return
        try:
            n=int(self.headers.get('Content-Length','0'))
            if n<=0 or n>128:raise ValueError('Invalid body size')
            if json.loads(self.rfile.read(n)).get('stationary_confirmed') is not True:raise ValueError('Stationary confirmation required')
            with network_lock:
                data,_=capture(9);s=parse_frequency(data)
                if s['baseline_state']==1:raise ValueError('Baseline already collecting; wait for completion')
                token=secrets.randbelow(65535)+1
                while token in (s['command_ack'],s['command_token']):token=secrets.randbelow(65535)+1
                data,_=capture(8,command=1,token=token);s=parse_frequency(data)
                deadline=time.monotonic()+2
                while s['command_ack']!=token and time.monotonic()<deadline:
                    time.sleep(.05);data,_=capture(9);s=parse_frequency(data)
                if s['command_ack']!=token:raise RuntimeError('Board did not acknowledge baseline command')
            log={'host_time':time.time(),'command_token':token,'user_confirmed_stationary':True,'board_ack':s['command_ack']}
            with (ROOT/'baseline_commands.jsonl').open('a',encoding='utf-8') as out:out.write(json.dumps(log)+'\n')
            data='板上已确认，正在采集静止基线。'.encode();self.send_response(200)
        except Exception as e:data=str(e).encode();self.send_response(400)
        self.send_header('Content-Type','text/plain; charset=utf-8');self.end_headers();self.wfile.write(data)
    def log_message(self,*a):pass
if __name__=='__main__':
    threading.Thread(target=update,daemon=True).start()
    ThreadingHTTPServer(('127.0.0.1',8769),Handler).serve_forever()
