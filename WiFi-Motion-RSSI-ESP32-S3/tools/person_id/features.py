"""Feature extraction shared by training and live inference."""
from __future__ import annotations
import numpy as np
FEATURE_VERSION = 1
def decode_frame(record: dict, expected_subcarriers: int | None = None) -> np.ndarray | None:
    raw = np.frombuffer(bytes.fromhex(record["csi_hex"]), dtype=np.int8).astype(np.float32)
    offset = 4 if record.get("first_word_invalid", False) else 0
    if raw.size <= offset or (raw.size - offset) % 2: return None
    iq = raw[offset:].reshape(-1, 2)
    if expected_subcarriers is not None and iq.shape[0] != expected_subcarriers: return None
    mag = np.sqrt(np.square(iq[:,0]) + np.square(iq[:,1]))
    mean = float(np.mean(mag))
    if not np.isfinite(mean) or mean <= 1e-6: return None
    return mag / mean
def window_features(records: list[dict], expected_subcarriers: int | None = None) -> tuple[np.ndarray, int] | None:
    vecs=[]; rssis=[]; subcarriers=expected_subcarriers
    for r in records:
        v=decode_frame(r, subcarriers)
        if v is None: continue
        if subcarriers is None: subcarriers=int(v.size)
        vecs.append(v); rssis.append(float(r["rssi_dbm"]))
    if subcarriers is None or len(vecs)<2: return None
    m=np.stack(vecs); temporal=np.abs(np.diff(m, axis=0))
    f=np.concatenate([np.mean(m,axis=0), np.std(m,axis=0), np.mean(temporal,axis=0), np.array([np.mean(rssis),np.std(rssis)],dtype=np.float32)]).astype(np.float32)
    return f, subcarriers
