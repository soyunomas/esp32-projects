#!/usr/bin/env python3
"""Run live host-side identification from the ESP32 CSI_RAW_V1 stream."""
from __future__ import annotations
import argparse, collections, sys
import joblib, serial
from capture import parse_raw_line
from features import FEATURE_VERSION, window_features

def main():
    p=argparse.ArgumentParser(); p.add_argument("--port",required=True); p.add_argument("--baud",type=int,default=115200); p.add_argument("--model",required=True); p.add_argument("--step",type=int,default=10); a=p.parse_args()
    artifact=joblib.load(a.model)
    if artifact.get("feature_version")!=FEATURE_VERSION: raise ValueError("Model feature version does not match this code")
    wf=int(artifact["window_frames"]); subs=int(artifact["subcarriers"]); model=artifact["model"]; window=collections.deque(maxlen=wf); since=0
    with serial.Serial(a.port,a.baud,timeout=1) as ser:
        print(f"Listening on {a.port}; window={wf} frames, subcarriers={subs}")
        while True:
            try: line=ser.readline().decode("utf-8",errors="replace").strip()
            except KeyboardInterrupt: return 0
            r=parse_raw_line(line)
            if r is None: continue
            window.append(r); since+=1
            if len(window)<wf or since<a.step: continue
            since=0; out=window_features(list(window),subs)
            if out is None: continue
            f,_=out; probs=model.predict_proba([f])[0]; classes=model.classes_; order=probs.argsort()[::-1]; print(" ".join(f"{classes[i]}={probs[i]:.3f}" for i in order[:3])); sys.stdout.flush()
if __name__=="__main__": raise SystemExit(main())
