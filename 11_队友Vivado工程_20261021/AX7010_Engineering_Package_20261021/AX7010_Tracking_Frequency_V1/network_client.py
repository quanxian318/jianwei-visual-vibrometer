"""Direct-LAN UDP snapshots. No external network services or board control."""
import argparse,json,socket,struct,time,threading,io,random
from pathlib import Path
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
import numpy as np
from PIL import Image
ROOT=Path(__file__).parent
def capture(kind=1,timeout=5,command=0,token=0):
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,4*1024*1024)
        sock.bind(('192.168.1.100',0));sock.settimeout(.08)
        deadline=time.monotonic()+timeout;next_request=0;frame=None;parts=set();data=None
        while time.monotonic()<deadline:
            now=time.monotonic()
            if now>=next_request:
                sock.sendto(b'AXS1'+bytes([kind,command,token&255,(token>>8)&255]),('192.168.1.10',5005));next_request=now+.2+random.random()*.15
            try: packet,peer=sock.recvfrom(2048)
            except socket.timeout: continue
            if peer[0]!='192.168.1.10' or len(packet)<33:continue
            magic,seq,w,h,fmt,total,offset,stamp=struct.unpack_from('<8I',packet)
            payload=packet[32:]
            if magic!=0x31565841 or total>640*360*2:continue
            if fmt==4:
                if kind not in (8,9) or w!=8192 or h!=1:continue
            elif fmt==3:
                if kind!=5 or w!=49408 or h!=1:continue
            elif (w,h) not in ((320,180),(640,360),(96,96)):continue
            expected=w*h*(2 if fmt==1 else 1)
            if fmt not in (1,2,3,4) or total!=expected or offset%1400 or offset+len(payload)>total:continue
            if len(payload)!=min(1400,total-offset):continue
            if frame!=seq:
                frame=seq;parts=set();data=bytearray(total)
            data[offset:offset+len(payload)]=payload;parts.add(offset)
            if len(parts)==(total+1399)//1400:
                if fmt in (3,4):
                    return bytes(data),{'sequence':seq,'width':w,'height':h,'format':fmt,'board_ticks_low32':stamp,'received_at':time.time(),'bytes':total}
                if fmt==1:
                    v=np.frombuffer(data,dtype='<u2').reshape(h,w)
                    rgb=np.stack([((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31],axis=-1).astype(np.uint8)
                else:
                    v=np.frombuffer(data,np.uint8).reshape(h,w)
                    t=np.clip((v.astype(float)-1)/63,0,1)
                    rgb=np.uint8(np.stack([np.maximum(0,t*2-1),1-np.abs(t*2-1),np.maximum(0,1-t*2)],axis=-1)*255)
                    rgb[v==0]=48
                return Image.fromarray(rgb),{'sequence':seq,'width':w,'height':h,'format':fmt,'board_ticks_low32':stamp,'received_at':time.time(),'bytes':total}
        raise TimeoutError('No complete Ethernet snapshot received')
def save(kind):
    im,meta=capture(kind)
    stem={1:'screen',2:'left',3:'right',4:'depth',5:'tracking',6:'roi_left',7:'roi_right'}[kind]
    if kind==5:(ROOT/'latest_tracking.bin').write_bytes(im)
    else:im.save(ROOT/f'latest_{stem}.png')
    (ROOT/f'latest_{stem}.json').write_text(json.dumps(meta,indent=2))
    return im,meta
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--kind',type=int,default=1,choices=range(1,8));parser.add_argument('--serve',action='store_true');args=parser.parse_args()
    if not args.serve:
        _,meta=save(args.kind);print(json.dumps(meta));raise SystemExit()
    state={'error':'Waiting for first image','meta':{}}
    latest={'jpeg':None}
    def update():
        while True:
            try:
                started=time.monotonic();im,meta=save(1)
                meta['capture_seconds']=time.monotonic()-started
                encoded=io.BytesIO();im.save(encoded,format='JPEG',quality=90)
                latest['jpeg']=encoded.getvalue()
                state['meta']=meta;state['error']=''
            except Exception as e:state['error']=str(e)
            time.sleep(.03)
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.startswith('/status'):
                body=json.dumps(state).encode();mime='application/json'
            elif self.path.startswith('/image'):
                body=latest['jpeg']
                if body is None:self.send_error(503);return
                mime='image/jpeg'
            elif self.path=='/':
                body='''<!doctype html><meta charset="utf-8"><title>AX7010 网口画面监看</title><style>body{background:#171b23;color:#eee;font:16px sans-serif}img{width:min(100%,1280px)}p{color:#bbb}</style><h2>AX7010 网口画面监看</h2><p>640×360 监看图，不是振动测量采样率；开发板 192.168.1.10</p><img id="v"><pre id="s"></pre><script>setInterval(async()=>{v.src="/image?t="+Date.now();let t=await(await fetch("/status")).json();t.image_age_seconds=t.meta.received_at?Date.now()/1000-t.meta.received_at:null;s.textContent=JSON.stringify(t,null,2)},250)</script>'''.encode('utf-8')
                mime='text/html; charset=utf-8'
            else:self.send_error(404);return
            self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
        def log_message(self,*args):pass
    threading.Thread(target=update,daemon=True).start()
    print('Local preview: http://127.0.0.1:8766',flush=True)
    ThreadingHTTPServer(('127.0.0.1',8766),Handler).serve_forever()
