import serial,time,json,csv,math,socket
from datetime import datetime
from pathlib import Path

LOG=Path("/home/uw/Documents/PRISM/logs")
PORT="/dev/ttyACM0"
ts=datetime.now().strftime("%Y%m%d_%H%M%S")
csvp=LOG/f"stream_{ts}.csv"
jsonl=LOG/f"stream_{ts}.jsonl"
evp=LOG/f"settle_events_{ts}.jsonl"
anp=LOG/f"settle_analysis_{ts}.txt"
open(LOG/"ACTIVE_STREAM.txt","w").write(str(csvp)+"\n")

ser=serial.Serial(PORT,115200,timeout=0.7)
time.sleep(0.3); ser.reset_input_buffer()
ser.write(b'\r\n'); ser.flush(); time.sleep(0.25); ser.read(8192)

def cli(cmd, wait=0.8):
    ser.reset_input_buffer()
    ser.write((cmd+'\r\n').encode()); ser.flush(); time.sleep(wait)
    t=''.join(ch if ch=='\n' or 32<=ord(ch)<127 else '' for ch in ser.read(16384).decode('utf-8','replace'))
    for ln in t.splitlines():
        s=ln.strip()
        if any(k in s for k in ('State','Axis','Failed','ERROR','started','enabled','cleared','set','Set')):
            print(' ',s)
    return t

print("=== ARM PLANT ===", flush=True)
for c,w in [
    ('valve_stop',0.8),
    ('odrive_disable',1.0),
    ('odrive_clear',1.6),('odrive_enable',2.8),('odrive_status',1.3),
    ('valve_vel_source 2',0.65),('valve_vel_lpf 30',0.65),
    ('valve_quiet 1',0.65),('valve_epsilon 0.15',0.65),
    ('valve_damping 0.1',0.6),('valve_friction 0.2',0.6),
    ('valve_wall_k 35',0.6),('valve_wall_c 0.6',0.6),
    ('valve_torquelimit 8',0.6),
    ('eth_stream start 10',0.9),
    ('valve_stop',0.8),  # ensure IDLE before start
    ('valve_start',2.2),('valve_status',1.1),('odrive_status',1.2),
]:
    print('>>>',c, flush=True)
    t=cli(c,w)
    if c=='odrive_status' and '0x08' not in t and 'CLOSED' not in t.upper():
        # valve_start also requests closed-loop; warn if still idle after enable
        if 'Axis state' in t:
            print('  WARN: ODrive not closed-loop yet (valve_start will re-request)', flush=True)
    if c=='valve_status' and 'RUNNING' not in t:
        print('FATAL: valve not RUNNING — abort', flush=True)
        raise SystemExit(2)

sock=socket.create_connection(("10.0.1.15",8888),timeout=5)
sock.settimeout(0.35)
buf=b""; end=time.time()+1.5
while time.time()<end:
    try:
        c=sock.recv(4096)
        if not c: break
        buf+=c
        if b'\n' in buf: break
    except socket.timeout: break
left=b'\n'.join(buf.split(b'\n')[1:])
fields=["wall_time","timestamp_ms","t_us","loop_time_us","seq","position_turns","position_deg","omega_rad_s","torque_nm","filt_torque_nm","status","passivity_mj","quiet","err","hb_age","data_valid"]
jf=open(jsonl,"w",buffering=1); cf=open(csvp,"w",newline="",buffering=1)
wri=csv.DictWriter(cf,fieldnames=fields,extrasaction="ignore"); wri.writeheader()
log_buf=left; rows=[]

def drain(sec):
    global log_buf
    t_end=time.time()+sec
    while time.time()<t_end:
        try: c=sock.recv(8192)
        except socket.timeout: c=b''
        if c: log_buf+=c
        while b'\n' in log_buf:
            line,log_buf=log_buf.split(b'\n',1)
            line=line.strip()
            if not line: continue
            try: s=json.loads(line)
            except: continue
            if 'position_deg' not in s and 'seq' not in s: continue
            wall=time.time()
            row={k:s.get(k) for k in fields if k!='wall_time'}; row['wall_time']=f'{wall:.6f}'
            jf.write(json.dumps(s)+'\n'); wri.writerow(row)
            try:
                rows.append(dict(wt=wall,pos=float(s.get('position_deg') or 0),
                    omega=float(s.get('omega_rad_s') or 0),tau=float(s.get('torque_nm') or 0),
                    quiet=bool(s.get('quiet'))))
            except: pass

events=[]
def hold(phase, seconds, note, human=False, action_s=0):
    """action_s: expected action duration at start of window (rest is settle)"""
    print("\n" + "!"*70, flush=True)
    if human:
        print(f">>> YOUR TURN — {note}", flush=True)
        print(f"    Countdown, then MOVE. After action, HANDS OFF until window ends.", flush=True)
        for i in range(5,0,-1):
            print(f"    {i}...", flush=True); time.sleep(1)
        print(f"    >>> GO NOW — move for ~{action_s}s then HANDS OFF", flush=True)
    else:
        print(f">>> HANDS OFF entire time — {note}", flush=True)
    t0=time.time()
    events.append(dict(phase=phase,t0=t0,human=human,note=note,action_s=action_s))
    while time.time()-t0 < seconds:
        drain(0.1)
    events[-1]['t1']=time.time()
    print(f"    === end {phase} ===", flush=True)

print("\n*** STAY AT THE HANDLE FOR ~45s TOTAL ***", flush=True)
print(f"stream={csvp.name}", flush=True)

hold('R0_rest', 4, 'baseline rest', human=False)
hold('M1_flick', 15, 'FLICK the lever mid-range HARD once (not at end stop)', human=True, action_s=2)
hold('R1_rest', 5, 'must be still after mid flick', human=False)
hold('E1_wall', 16, 'PUSH into END STOP then let go so it rebounds; hands off after', human=True, action_s=4)
hold('R2_rest', 6, 'must be still after end-stop', human=False)

print("\n>>> STOP", flush=True)
cli('valve_stop',1.0); cli('odrive_disable',1.3); cli('valve_status',1.0); cli('odrive_status',1.2)
ser.close(); drain(0.4); jf.close(); cf.close(); sock.close()
with open(evp,'w') as f:
    for e in events: f.write(json.dumps(e)+'\n')

def absmean(xs): return sum(abs(x) for x in xs)/len(xs) if xs else 0
def pp(xs): return max(xs)-min(xs) if xs else 0

print("\n=== RESULTS ===", flush=True)
print(f"{'phase':<12} {'motion?':>8} {'|ω|last1s':>10} {'|τ|last1s':>10} {'pos_pp':>8} {'q%':>5} settle")
lines=[]
for e in events:
    segs=[r for r in rows if e['t0']<=r['wt']<=e['t1']]
    if not segs:
        print(e['phase'],'NO DATA'); continue
    last=[r for r in segs if r['wt']>=e['t1']-1.0] or segs[-50:]
    # first 3s for motion detect on human windows
    early=[r for r in segs if r['wt']<=e['t0']+4.0]
    pos_pp=pp([r['pos'] for r in segs])
    early_pp=pp([r['pos'] for r in early]) if early else 0
    w_abs=absmean([r['omega'] for r in last])
    t_abs=absmean([r['tau'] for r in last])
    q=sum(1 for r in last if r['quiet'])/len(last)
    moved = pos_pp > 3.0 or absmean([r['omega'] for r in early]) > 0.2
    settled = (w_abs < 0.12 and pp([r['pos'] for r in last]) < 2.0) or q > 0.7
    if e.get('human') and not moved:
        flag='MISS'
    elif settled:
        flag='PASS'
    else:
        flag='FAIL'
    line=f"{e['phase']:<12} {'YES' if moved else 'no':>8} {w_abs:10.4f} {t_abs:10.4f} {pos_pp:8.2f} {100*q:4.0f}% {flag}"
    print(line, flush=True)
    lines.append(line)
    if e.get('human') and moved:
        t0=e['t0']
        for sec in range(0, min(16, int(e['t1']-t0)+1)):
            binr=[r for r in segs if sec<=r['wt']-t0<sec+1]
            if not binr: continue
            print(f"    +{sec:02d}s |ω|={absmean([r['omega'] for r in binr]):.3f} |τ|={absmean([r['tau'] for r in binr]):.3f} "
                  f"pos=[{min(x['pos'] for x in binr):.1f},{max(x['pos'] for x in binr):.1f}] "
                  f"q%={100*sum(1 for r in binr if r['quiet'])/len(binr):.0f}", flush=True)

summary="SETTLE VERIFY (continue)\n"+f"stream={csvp.name}\n"+"\n".join(lines)+"\n"
summary+="PASS = last 1s nearly still (|ω|<0.12, pos stable) or quiet>70%\n"
summary+="MISS = no handle motion logged in action window\n"
anp.write_text(summary)
print(f"\n{summary}\nwrote {anp}", flush=True)
print("DONE", flush=True)