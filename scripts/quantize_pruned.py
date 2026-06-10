#!/usr/bin/env python3
"""
ESP-PPQ int8 量化 pruned_keep14.onnx → pruned_keep14_v2.espdl
"""
import os, sys

# Point to original project scripts for dataset.py
sys.path.insert(0, "/mnt/d/ESP-32/workplace/esp32-audio-cls/scripts")
from dataset import MelSpectrogramDataset

import numpy as np
import torch
from torch.utils.data import DataLoader

# --- Calibration Dataset ---
class CalibrationDataset(torch.utils.data.Dataset):
    def __init__(self, data_root: str, max_samples: int = 200, seed: int = 42):
        ds = MelSpectrogramDataset(data_root, split="train", seed=seed, augment=False)
        rng = np.random.default_rng(seed)
        indices = rng.choice(len(ds), min(max_samples, len(ds)), replace=False)
        self.samples = [ds[i][0] for i in indices]

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        return self.samples[idx]

# --- Main ---
CALIB_DIR = "/mnt/d/ESP-32/workplace/esp32-audio-cls/data/processed"
ONNX_PATH = "/mnt/d/ESP-32/workplace/esp32-audio-cls/models/pruned_keep14.onnx"
OUT_DIR = "/home/mikcslu/projects/esp32-p4-edge-classifier/models/"
OUT_NAME = "pruned_keep14_v2.espdl"

os.makedirs(OUT_DIR, exist_ok=True)

print("=" * 50)
print("ESP-PPQ INT8 量化: pruned_keep14")
print(f"  输入: {ONNX_PATH}")
print(f"  标定: {CALIB_DIR}")
print(f"  输出: {OUT_DIR}{OUT_NAME}")
print("=" * 50)

# Create calibration DataLoader
calib_ds = CalibrationDataset(CALIB_DIR, max_samples=200)
calib_loader = DataLoader(calib_ds, batch_size=1, shuffle=True)
print(f"  标定集: {len(calib_ds)} 样本, shape={calib_ds[0].shape}")

# Run ESP-PPQ
from esp_ppq.api import espdl_quantize_onnx

output_path = os.path.join(OUT_DIR, OUT_NAME)
input_shape = [1, 1, 128, 100]

print(f"\n开始量化...")
ppq_graph = espdl_quantize_onnx(
    onnx_import_file=ONNX_PATH,
    espdl_export_file=output_path,
    calib_dataloader=calib_loader,
    calib_steps=32,
    input_shape=input_shape,
    target="esp32p4",
    num_of_bits=8,
    device="cpu",
    error_report=True,
)

print(f"\n量化完成!")
print(f"  输出: {output_path}")

# Show file sizes
for f in sorted(os.listdir(OUT_DIR)):
    if "pruned_keep14_v2" in f:
        size = os.path.getsize(os.path.join(OUT_DIR, f)) / 1024
        print(f"  {f}: {size:.1f} KB")

print(f"\n  原始 ONNX: {os.path.getsize(ONNX_PATH) / 1024:.1f} KB")
print(f"  完成 ✅")
