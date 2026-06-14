"""
Setup script for litert.
Installs litert-lm-api and downloads a LiteRT-LM model.

Usage:
    python scripts/setup.py                          # Gemma 4 E2B (default)
    python scripts/setup.py --model gemma3-1b        # Gemma 3 1B INT4 (529 MB)
    python scripts/setup.py --model qwen3-0.6b      # Qwen3 0.6B INT8 (586 MB)
    python scripts/setup.py --model qwen3-4b        # Qwen3 4B INT4 (2.5 GB)
    python scripts/setup.py --model qwen3-0.6b --variant int4
    python scripts/setup.py --model-dir D:/models
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).parent.parent

MODELS = {
    "gemma4-e2b": {
        "repo": "litert-community/gemma-4-E2B-it-litert-lm",
        "default_variant": "generic",
        "variants": {
            "generic":         ("gemma-4-E2B-it.litertlm",               "2.6 GB — CPU/GPU (recommended)"),
            "qualcomm_sm8750": ("gemma-4-E2B-it_qualcomm_sm8750.litertlm","2.8 GB — Snapdragon 8 Elite"),
            "intel_PTL":       ("gemma-4-E2B-it_intel_PTL.litertlm",      "2.8 GB — Intel Panther Lake"),
        },
    },
    "qwen3-0.6b": {
        "repo": "litert-community/Qwen3-0.6B",
        "default_variant": "int8",
        "variants": {
            "int8": ("Qwen3-0.6B.litertlm",           "586 MB — INT8 (recommended)"),
            "int4": ("qwen3_0_6b_mixed_int4.litertlm", "475 MB — INT4, 2048 ctx"),
        },
    },
    "qwen3-4b": {
        "repo": "litert-community/Qwen3-4B",
        "default_variant": "int4",
        "variants": {
            "int4": ("qwen3_4b_mixed_int4.litertlm",                 "2.5 GB — INT4, 2048 ctx (recommended)"),
            "int8": ("qwen3_4b_channelwise_int8_float32kv.litertlm", "5.3 GB — INT8"),
        },
    },
    "gemma3-1b": {
        "repo": "litert-community/Gemma3-1B-IT",
        "default_variant": "int4",
        "variants": {
            "int4": ("gemma3-1b-it-int4.litertlm", "529 MB — INT4 QAT, 2048 ctx"),
        },
    },
}


def install_litert():
    print("Checking litert-lm-api...")
    try:
        import litert_lm
        print(f"  Already installed: {litert_lm.__file__}")
        return True
    except ImportError:
        pass
    print("  Installing litert-lm-api...")
    result = subprocess.run(
        [sys.executable, "-m", "pip", "install", "litert-lm-api"],
        check=False
    )
    return result.returncode == 0


def download_model(repo: str, filename: str, desc: str, model_dir: Path):
    from huggingface_hub import hf_hub_download

    dest = model_dir / filename
    if dest.exists():
        size_gb = dest.stat().st_size / 1e9
        print(f"  Model already present: {dest} ({size_gb:.2f} GB)")
        return dest

    print(f"  Downloading {filename} ({desc})...")
    model_dir.mkdir(parents=True, exist_ok=True)

    path = hf_hub_download(
        repo_id=repo,
        filename=filename,
        local_dir=str(model_dir),
    )
    size_gb = Path(path).stat().st_size / 1e9
    print(f"  Saved: {path} ({size_gb:.2f} GB)")
    return Path(path)


def main():
    parser = argparse.ArgumentParser(description="Setup litert project")
    parser.add_argument("--model", default="gemma4-e2b", choices=list(MODELS),
                        help="Model to download (default: gemma4-e2b)")
    parser.add_argument("--variant", default=None,
                        help="Model variant (default depends on --model)")
    parser.add_argument("--model-dir", default=str(ROOT / "models"),
                        help="Directory to save model files (default: ./models)")
    parser.add_argument("--skip-litert", action="store_true",
                        help="Skip litert-lm-api installation check")
    args = parser.parse_args()

    model_info = MODELS[args.model]
    variant = args.variant or model_info["default_variant"]
    if variant not in model_info["variants"]:
        valid = ", ".join(model_info["variants"])
        print(f"ERROR: Unknown variant '{variant}' for {args.model}. Valid: {valid}")
        sys.exit(1)

    filename, desc = model_info["variants"][variant]
    model_dir = Path(args.model_dir)

    print("=== litert setup ===\n")
    print(f"Model : {args.model}  variant={variant}")

    if not args.skip_litert:
        if not install_litert():
            print("ERROR: Failed to install litert-lm-api")
            sys.exit(1)

    print(f"\nDownloading model...")
    model_path = download_model(model_info["repo"], filename, desc, model_dir)

    print(f"\n=== Done ===")
    print(f"Model: {model_path}")
    print(f"\nRun the chat:")
    print(f'  litert_chat.exe "{model_path}" --gpu')


if __name__ == "__main__":
    main()
