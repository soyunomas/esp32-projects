#!/usr/bin/env python3
"""Train and session-wise evaluate a CSI person-identification baseline."""
from __future__ import annotations
import argparse, json
from collections import Counter, defaultdict
from pathlib import Path
import joblib, numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler
from features import FEATURE_VERSION, window_features

def read_jsonl(paths):
    records=[]
    for path in paths:
        with path.open("r",encoding="utf-8") as h:
            for n,line in enumerate(h,1):
                if not line.strip(): continue
                try: r=json.loads(line)
                except json.JSONDecodeError as e: raise ValueError(f"{path}:{n}: invalid JSON: {e}") from e
                for k in ("label","session","csi_hex","rssi_dbm"):
                    if k not in r: raise ValueError(f"{path}:{n}: missing {k}")
                records.append(r)
    return records

def make_windows(records, window_frames, stride):
    grouped=defaultdict(list)
    for r in records: grouped[(str(r["label"]),str(r["session"]))].append(r)
    for g in grouped.values(): g.sort(key=lambda x:int(x.get("received_us",0)))
    fs=[]; ys=[]; ss=[]; expected=None
    for (label,session),g in sorted(grouped.items()):
        for start in range(0,max(0,len(g)-window_frames+1),stride):
            out=window_features(g[start:start+window_frames],expected)
            if out is None: continue
            f,subs=out
            if expected is None: expected=subs
            if subs!=expected: continue
            fs.append(f); ys.append(label); ss.append(session)
    if expected is None or not fs: raise ValueError("No usable windows. Check CSI lengths and capture duration.")
    return np.stack(fs),np.asarray(ys),np.asarray(ss),expected

def make_model():
    return Pipeline([("scale",StandardScaler()),("classifier",LogisticRegression(max_iter=3000,class_weight="balanced"))])

def main():
    p=argparse.ArgumentParser(); p.add_argument("inputs",nargs="+",type=Path); p.add_argument("--window-frames",type=int,default=40); p.add_argument("--stride",type=int,default=20); p.add_argument("--model-out",type=Path,default=Path("person_id_model.joblib")); a=p.parse_args()
    if a.window_frames<2 or a.stride<1: p.error("window-frames must be >=2 and stride >=1")
    records=read_jsonl(a.inputs); X,y,sessions,subs=make_windows(records,a.window_frames,a.stride)
    print(f"frames={len(records)} windows={len(y)} subcarriers={subs}"); print("windows/class:",dict(Counter(y)))
    all_classes=set(y); truth=[]; pred=[]; skipped=[]
    for held in sorted(set(sessions)):
        test=sessions==held; train=~test
        if not np.any(test) or not np.any(train) or set(y[train])!=all_classes: skipped.append(held); continue
        m=make_model(); m.fit(X[train],y[train]); p_=m.predict(X[test]); print(f"held-out session {held}: n={int(np.sum(test))} accuracy={accuracy_score(y[test],p_):.3f}"); truth.extend(y[test].tolist()); pred.extend(p_.tolist())
    if truth:
        labels=sorted(all_classes); print(f"session-wise accuracy={accuracy_score(truth,pred):.3f}"); print("confusion matrix labels:",labels); print(confusion_matrix(truth,pred,labels=labels)); print(classification_report(truth,pred,labels=labels,zero_division=0))
    else: print("WARNING: no valid held-out-session folds. Record at least two independent sessions per person.")
    if skipped: print("skipped held-out sessions:",skipped)
    final=make_model(); final.fit(X,y); a.model_out.parent.mkdir(parents=True,exist_ok=True); joblib.dump({"feature_version":FEATURE_VERSION,"window_frames":a.window_frames,"subcarriers":subs,"classes":sorted(all_classes),"model":final},a.model_out); print(f"saved {a.model_out}"); return 0
if __name__=="__main__": raise SystemExit(main())
