"""Signal-only amplitude bench: close PulseView, connect U/V/W and common GND.
Uses /tmp/vvvf_capture; restores jr-fall/0 Hz on exit. Overwrites angle-*.sr files.
"""
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,'tools')
from measure import save_capture
import os,time,termios,select,subprocess,pathlib,json,re,math
root=pathlib.Path(__file__).resolve().parents[1];fs=12000000
def main():
    fd=os.open('/dev/ttyACM0',os.O_RDWR|os.O_NONBLOCK|os.O_NOCTTY)
    original=termios.tcgetattr(fd);a=termios.tcgetattr(fd);a[0]=a[1]=a[3]=0;termios.tcsetattr(fd,termios.TCSANOW,a)
    p=None;results=[]
    def send(s):os.write(fd,(s+'\n').encode())
    def drain(seconds):
     end=time.monotonic()+seconds;out=[]
     while time.monotonic()<end:
      if select.select([fd],[],[],.03)[0]:out.append(os.read(fd,8192).decode(errors='replace'))
     return ''.join(out)
    def capture(label,seconds,actions):
     nonlocal p
     path=root/'build/captures'/f'{label}.bin'
     p=subprocess.Popen(['/tmp/vvvf_capture',str(path),str(int(fs*seconds)),str(fs)],stderr=subprocess.PIPE,text=True)
     while True:
      line=p.stderr.readline()
      if line.startswith('CAPTURE_STARTED'):break
      if not line and p.poll() is not None:raise RuntimeError('analyzer failed to start')
     start=time.monotonic();logs=[];host_events=[]
     while p.poll() is None:
      elapsed=time.monotonic()-start
      while actions and elapsed>=actions[0][0]:
       _,command=actions.pop(0);send(command);host_events.append({'time':elapsed,'command':command})
      if select.select([fd],[],[],.01)[0]:logs.append(os.read(fd,16384).decode(errors='replace'))
      if elapsed>seconds+10:raise RuntimeError('capture timeout')
     if p.returncode:raise RuntimeError(p.stderr.read())
     raw=path.read_bytes()
     if len(raw)!=int(fs*seconds):raise RuntimeError('short capture')
     if len(set(raw.translate(bytes(x&7 for x in range(256)))))<2:raise RuntimeError('no signal')
     log=''.join(logs)
     if 'fault=1' in log:raise RuntimeError(log)
     save_capture(root/'Doc/captures'/f'{label}.sr',raw,fs)
     # Only complete U-high runs; exclude runs clipped by capture boundaries.
     bits=raw.translate(bytes(x&1 for x in range(256)))
     widths=sorted(m.end()-m.start() for m in re.finditer(b'\x01+',bits) if m.start()>0 and m.end()<len(bits))
     uv=raw.translate(bytes((x^(x>>1))&1 for x in range(256)))
     return {'capture':label+'.sr','seconds':seconds,'samplerate':fs,'rising_U':len(re.findall(b'\x00\x01',bits)),
             'U_high_min_us':widths[0]*1e6/fs,'U_high_median_us':widths[len(widths)//2]*1e6/fs,
             'U_high_max_us':widths[-1]*1e6/fs,'UV_nonzero_fraction':uv.count(b'\x01')/len(uv),'log':log.splitlines(),'host_events':host_events}
    try:
     for profile in ['jr-fall','pmsm-toshiba']:
      send('stop');time.sleep(.1);send('profile '+profile);time.sleep(.1);send('f 65');time.sleep(.15);drain(.1)
      for m in [0,.25,.5,.7,.8,.9,.99,.99999,1]:
       send('m '+str(m));drain(.15)
       row=capture(f'angle-{profile}-m{m:g}',1,[]);row.update(profile=profile,hz=65,requested_m=m)
       angle=60*(1-m) if profile=='jr-fall' else 38*(1-m)**.72
       row['expected_angle_degrees']=angle;row['expected_aux_us']=angle/360/65*1e6
       results.append(row);print(json.dumps(row),flush=True)
      send('m .7');drain(.1)
      values=[.7,.75,.8,.85,.9,.95,1,.95,.9,.85,.8,.75,.7]
      actions=[(.2+i*.12,'m '+str(m)) for i,m in enumerate(values)]
      row=capture('angle-'+profile+'-changing',2.2,actions);row.update(profile=profile,hz=65,requested_m='changing .7->1->.7');results.append(row);print(json.dumps(row),flush=True)
    finally:
     send('stop');time.sleep(.1);send('profile jr-fall');print('RESTORED:',drain(.6),flush=True)
     if p and p.poll() is None:p.terminate();p.wait(timeout=5)
     termios.tcsetattr(fd,termios.TCSANOW,original);os.close(fd)
    (root/'Doc/captures/amplitude-summary.json').write_text(json.dumps(results,indent=2)+'\n')

if __name__ == "__main__":
    main()
