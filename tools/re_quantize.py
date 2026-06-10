#!/usr/bin/env python3
"""
Re-quantize ESP-DL model (no torch dependency, pure numpy).
用法: python3 re_quantize.py --onnx <onnx_file> --calib-dir <dir>
"""
import argparse, os, sys, glob, time
from pathlib import Path
import numpy as np

class NumpyCalibDataLoader:
    """A simple dataloader that yields numpy arrays from a directory."""

    def __init__(self, data_dir: str, max_samples: int = 200, batch_size: int = 1):
        self.batch_size = batch_size
        files = sorted(glob.glob(os.path.join(data_dir, "*.npy")))
        if not files:
            print(f"  WARNING: no .npy files in {data_dir}, using random data")
            sample = np.random.randn(1, 128, 100).astype(np.float32)
            self.samples = [sample for _ in range(max_samples)]
        else:
            rng = np.random.default_rng(42)
            idxs = rng.choice(len(files), min(len(files), max_samples), replace=False)
            self.samples = [np.load(files[i]).astype(np.float32) for i in idxs]
        print(f"  Loaded {len(self.samples)} calibration samples, shape={self.samples[0].shape}")

    def __iter__(self):
        for s in self.samples:
            yield s[np.newaxis, np.newaxis, ...]  # (1, 1, 128, 100)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--onnx", default="models/mobilenetv2_audio.onnx")
    parser.add_argument("--calib-dir", default="data/processed")
    parser.add_argument("--out", default="models/")
    parser.add_argument("--target", default="esp32p4")
    parser.add_argument("--calib-samples", type=int, default=200)
    parser.add_argument("--calib-steps", type=int, default=32)
    parser.add_argument("--num-bits", type=int, default=8)
    args = parser.parse_args()

    print("=" * 50)
    print("ESP-PPQ INT8 量化 (re-quantize)")
    print(f"  ONNX:  {args.onnx}")
    print(f"  目标:  {args.target}, {args.num_bits}-bit")
    print(f"  标定:  {args.calib_samples} 样本, {args.calib_steps} 步")
    print("=" * 50)

    calib_loader = NumpyCalibDataLoader(args.calib_dir, max_samples=args.calib_samples)
    input_shape = [1, 1, 128, 100]

    # Lazy import esp-ppq
    try:
        from esp_ppq.api import espdl_quantize_onnx
    except ImportError:
        print("ERROR: esp-ppq not installed. Run: pip install esp-ppq")
        sys.exit(1)

    output_path = os.path.join(args.out, "mobilenetv2_audio_req.espdl")
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)

    print(f"\n开始量化...")
    t0 = time.time()

    try:
        ppq_graph = espdl_quantize_onnx(
            onnx_import_file=args.onnx,
            espdl_export_file=output_path,
            calib_dataloader=calib_loader,
            calib_steps=args.calib_steps,
            input_shape=input_shape,
            target=args.target,
            num_of_bits=args.num_bits,
            device="cpu",
            error_report=True,
        )
    except TypeError as e:
        print(f"\n  参数错误: {e}")
        print("  尝试兼容模式...")
        # Try without error_report
        ppq_graph = espdl_quantize_onnx(
            onnx_import_file=args.onnx,
            espdl_export_file=output_path,
            calib_dataloader=calib_loader,
            calib_steps=args.calib_steps,
            input_shape=input_shape,
            target=args.target,
            num_of_bits=args.num_bits,
            device="cpu",
        )

    elapsed = time.time() - t0
    size = os.path.getsize(output_path) / 1024
    print(f"\n量化完成!")
    print(f"  耗时:  {elapsed:.1f}s")
    print(f"  输出:  {output_path}")
    print(f"  大小:  {size:.1f} KB")

    # Also generate .h file
    h_path = output_path.replace(".espdl", ".h")
    print(f"\n生成 C header...")
    with open(output_path, "rb") as f:
        data = f.read()

    var_name = "audio_model_data"
    with open(h_path, "w") as h:
        h.write(f"// Auto-generated from {os.path.basename(output_path)}\n")
        h.write(f"// Generated: {time.ctime()}\n")
        h.write(f"const unsigned char {var_name}[] = {{\n")
        for i in range(0, len(data), 12):
            chunk = data[i:i + 12]
            h.write("  " + ", ".join(f"0x{b:02x}" for b in chunk))
            if i + 12 < len(data):
                h.write(",")
            h.write("\n")
        h.write(f"}};\n")
        h.write(f"const unsigned int audio_model_data_len = {len(data)};\n")

    print(f"  C header: {h_path}")
    print(f"\n完成 ✅")


if __name__ == "__main__":
    main()
