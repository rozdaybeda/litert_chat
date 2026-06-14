"""
Multi-model translation benchmark.
Tests GPU and CPU translation speed across all downloaded models and 6 European languages.

Usage:
    python bench_models.py           # GPU + CPU combined table
    python bench_models.py --gpu     # GPU only
    python bench_models.py --cpu     # CPU only
"""
import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

os.environ["GLOG_minloglevel"] = "3"
os.environ["PYTHONUTF8"] = "1"

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT    = Path(__file__).parent.parent
MODELS  = ROOT / "models"
EXE     = ROOT / "build" / "clang-x64-release" / "litert_chat.exe"

PHRASE = "The weather is beautiful today, let's go for a walk."
LANGS  = ["French", "German", "Spanish", "Italian", "Ukrainian", "Polish"]

# (path, no_think, gpu_supported)
MODEL_FILES = {
    "Gemma 4 E2B":  (MODELS / "gemma-4-E2B-it.litertlm",        False, True),
    "Gemma 3 1B":   (MODELS / "gemma3-1b-it-int4.litertlm",      False, True),
    "Qwen3 0.6B":   (MODELS / "Qwen3-0.6B.litertlm",             True,  True),
    "Qwen3 4B":     (MODELS / "qwen3_4b_mixed_int4.litertlm",     True,  False),  # mixed_int4 unsupported on GPU
}


def run_model(model_path: Path, backend: str, no_think: bool = False) -> tuple[int, list[dict]]:
    """Returns (load_ms, [{lang, ms, tok, tps}])."""
    commands = "\n".join(f"/translate {lang} {PHRASE}" for lang in LANGS)
    stdin = f"\n{commands}\n/quit\n"  # leading \n absorbs BOM

    cmd = [str(EXE), str(model_path), f"--{backend}"]
    if no_think:
        cmd.append("--no-think")

    result = subprocess.run(
        cmd,
        input=stdin.encode("utf-8"),
        capture_output=True,
        timeout=300,
    )
    out = result.stdout.decode("utf-8", errors="replace")

    load_ms = 0
    m = re.search(r"Ready!\s*\((\d+)\s*ms\)", out)
    if m:
        load_ms = int(m.group(1))

    stats = re.findall(r"\[(\d+)\s*ms\s*\|\s*(\d+)\s*tok\s*\|\s*([\d.]+)\s*tok/s\]", out)
    results = []
    for i, (ms, tok, tps) in enumerate(stats):
        if i < len(LANGS):
            results.append({"lang": LANGS[i], "ms": int(ms), "tok": int(tok), "tps": float(tps)})

    return load_ms, results


def bench_backend(available: dict, backend: str) -> dict:
    """Run all models on a backend. Returns {name: (load_ms, avg_ms, avg_tps) or None}."""
    out = {}
    for name, (path, no_think, gpu_ok) in available.items():
        effective = backend if (backend == "cpu" or gpu_ok) else "cpu"
        forced_cpu = backend == "gpu" and not gpu_ok
        print(f"    {name} [{effective.upper()}]...", end=" ", flush=True)
        try:
            load_ms, results = run_model(path, effective, no_think)
        except subprocess.TimeoutExpired:
            print("TIMEOUT")
            out[name] = None
            continue
        except Exception as e:
            print(f"ERROR: {e}")
            out[name] = None
            continue

        if not results:
            print("no output")
            out[name] = None
            continue

        avg_ms  = sum(r["ms"]  for r in results) / len(results)
        avg_tps = sum(r["tps"] for r in results) / len(results)
        tag = " *" if forced_cpu else ""
        print(f"load={load_ms}ms  avg={avg_ms:.0f}ms  {avg_tps:.1f} tok/s{tag}")
        out[name] = (load_ms, avg_ms, avg_tps, forced_cpu)
    return out


def fmt_cell(data, forced_cpu: bool = False) -> str:
    if data is None:
        return f"{'N/A':>6}  {'N/A':>6}  {'N/A':>8}"
    load_ms, avg_ms, avg_tps, _ = data
    tag = "*" if forced_cpu else " "
    return f"{load_ms:>5}ms  {avg_ms:>5.0f}ms  {avg_tps:>7.1f}{tag}"


def print_single(available: dict, results: dict, backend: str):
    W = 82
    print("=" * W)
    print(f"  Model Translation Benchmark ({backend.upper()}) — {len(LANGS)} languages")
    print("=" * W)
    print(f"  Phrase: \"{PHRASE}\"")
    print()
    hdr = f"  {'Model':<16}  {'Load':>7}  {'Avg ms':>7}  {'Avg tok/s':>9}"
    print(hdr)
    print("  " + "-" * (W - 2))
    for name in available:
        d = results.get(name)
        if d is None:
            print(f"  {name:<16}  {'FAILED'}")
            continue
        load_ms, avg_ms, avg_tps, forced_cpu = d
        tag = " (cpu*)" if forced_cpu else ""
        print(f"  {name:<16}  {load_ms:>5}ms  {avg_ms:>5.0f}ms  {avg_tps:>8.1f}{tag}")
    print("  " + "-" * (W - 2))
    if any(d and d[3] for d in results.values()):
        print("  * model forced to CPU (GPU not supported)")
    print("=" * W)


def print_combined(available: dict, gpu_res: dict, cpu_res: dict):
    W = 90
    print()
    print("=" * W)
    print(f"  GPU vs CPU | {len(LANGS)}-language translation benchmark")
    print("=" * W)
    print(f"  Phrase: \"{PHRASE}\"")
    print()
    hdr = (f"  {'Model':<16}  "
           f"{'--- GPU ---':^24}  "
           f"{'--- CPU ---':^24}")
    sub = (f"  {'':16}  "
           f"{'Load':>7}  {'Avg ms':>7}  {'tok/s':>7}  "
           f"{'Load':>7}  {'Avg ms':>7}  {'tok/s':>7}")
    print(hdr)
    print(sub)
    print("  " + "-" * (W - 2))
    for name in available:
        g = gpu_res.get(name)
        c = cpu_res.get(name)

        def fmt(d):
            if d is None:
                return f"{'N/A':>7}  {'N/A':>7}  {'N/A':>7}"
            load_ms, avg_ms, avg_tps, forced = d
            tag = "*" if forced else " "
            return f"{load_ms:>5}ms{tag} {avg_ms:>5.0f}ms  {avg_tps:>7.1f}"

        print(f"  {name:<16}  {fmt(g)}  {fmt(c)}")

    print("  " + "-" * (W - 2))
    if any(d and d[3] for d in {**gpu_res, **cpu_res}.values()):
        print("  * model forced to CPU (GPU backend not supported)")
    print("=" * W)


def main():
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--gpu", action="store_true", help="GPU only")
    group.add_argument("--cpu", action="store_true", help="CPU only")
    args = parser.parse_args()

    available = {name: v for name, v in MODEL_FILES.items() if v[0].exists()}
    missing   = {name: v[0] for name, v in MODEL_FILES.items() if not v[0].exists()}

    if missing:
        print("Skipping (not downloaded):")
        for name, path in missing.items():
            print(f"  {name}: {path.name}")
        print()

    if not available:
        print("No models found. Run: python scripts/setup.py --model <name>")
        sys.exit(1)

    if args.gpu:
        print("Running GPU benchmark...\n")
        res = bench_backend(available, "gpu")
        print_single(available, res, "gpu")
    elif args.cpu:
        print("Running CPU benchmark...\n")
        res = bench_backend(available, "cpu")
        print_single(available, res, "cpu")
    else:
        print("Running GPU benchmark...\n")
        gpu_res = bench_backend(available, "gpu")
        print("\nRunning CPU benchmark...\n")
        cpu_res = bench_backend(available, "cpu")
        print_combined(available, gpu_res, cpu_res)


if __name__ == "__main__":
    main()
