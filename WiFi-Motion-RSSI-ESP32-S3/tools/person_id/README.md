# CSI person identification proof of concept

This directory turns the ESP32-S3 CSI motion project into a data-collection and host-side person-identification experiment. It is intentionally a baseline, not a claim that the ESP32-S3 reproduces BFId beamforming-feedback results.

## 1. Enable raw CSI streaming

Run `idf.py menuconfig` and enable:

- `WiFi motion detector -> Stream raw CSI records for person-ID datasets`
- Keep the default `Raw CSI dataset minimum frame interval` at 50 ms initially.

Build and flash normally. The firmware continues to run the existing RSSI/CSI motion detector and additionally emits lines such as:

```text
CSI_RAW_V1,<received_us>,<wifi_timestamp_us>,<rssi_dbm>,<first_word_invalid>,<length>,<hex_iq_bytes>
```

The raw vector is emitted from a dedicated low-priority task, not from the Wi-Fi CSI callback.

## 2. Prepare the host tools

```bash
cd WiFi-Motion-RSSI-ESP32-S3/tools/person_id
python -m venv .venv
source .venv/bin/activate        # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

## 3. Record a dataset

Record multiple *independent sessions* for every person. A session should ideally be made after stopping/restarting capture, and later sessions should be recorded at a different time. Do not rely on one long recording split into smaller files.

Example for two people and two sessions each:

```bash
python capture.py --port /dev/ttyACM0 --label alice --session day1-a --duration 90 --output data/alice-day1-a.jsonl
python capture.py --port /dev/ttyACM0 --label bob   --session day1-a --duration 90 --output data/bob-day1-a.jsonl

# Repeat later, preferably after people leave/re-enter the monitored path.
python capture.py --port /dev/ttyACM0 --label alice --session day2-b --duration 90 --output data/alice-day2-b.jsonl
python capture.py --port /dev/ttyACM0 --label bob   --session day2-b --duration 90 --output data/bob-day2-b.jsonl
```

For a first useful experiment, target at least 2-3 people, 3 or more independent sessions per person, and multiple walk-throughs during every session. Keep the router and ESP32 fixed while collecting the initial dataset.

## 4. Train and evaluate

```bash
python train.py data/*.jsonl --model-out person_id_model.joblib
```

The baseline extracts, for each CSI window:

- normalized per-subcarrier mean magnitude;
- per-subcarrier magnitude standard deviation;
- per-subcarrier mean temporal absolute difference;
- mean and standard deviation of RSSI.

It trains a standardized multinomial logistic-regression classifier. Evaluation is leave-one-session-out: a complete session is held out at a time. This is deliberately stricter than randomly splitting overlapping windows, which would leak near-duplicate samples across train and test.

If the script reports that no held-out-session folds are valid, collect at least two independent session IDs for every class. Three or more are preferable.

## 5. Live host-side inference

After training:

```bash
python live.py --port /dev/ttyACM0 --model person_id_model.joblib
```

The tool prints the top class probabilities for sliding CSI windows.

## Experimental protocol

Use anonymous labels unless a real identity is required. For a meaningful validation:

1. Keep one full recording session unseen during model fitting.
2. Repeat the test on another day.
3. Have participants leave and re-enter between trials.
4. Include an `unknown` person during evaluation even though this simple closed-set baseline cannot yet reject unknown identities reliably.
5. Record chance accuracy (`1 / number_of_people`) next to model accuracy.
6. Do not interpret high accuracy from one room/session as general identity recognition.

## Current limitations

- ESP32-S3 provides CSI from 802.11n rather than the 802.11ac/ax beamforming-feedback information used by BFId.
- This baseline uses magnitude-derived features only; phase sanitization and sequence models are not implemented yet.
- The classifier runs on the host. Moving inference to the ESP32 should only be attempted after session-wise accuracy is demonstrably above chance.
- Raw CSI output increases UART traffic and is disabled by default.
- This is closed-set classification. A production system needs explicit unknown-person rejection and privacy controls.
