import argparse
import importlib.util
import os
from pathlib import Path
import sys


PROJECT = Path(__file__).resolve().parents[1]


def load_compiler(sdk_path=None):
    sdk_path = sdk_path or os.environ.get("VQEAF_SDK")
    if sdk_path is None:
        sdk_path = next((parent / "sdk" / "cli" / "qeaf.py"
                         for parent in PROJECT.parents
                         if (parent / "sdk" / "cli" / "qeaf.py").is_file()), None)
    if sdk_path is None:
        raise FileNotFoundError("SDK not found; use --sdk PATH/TO/sdk/cli/qeaf.py or VQEAF_SDK")
    sdk_path = Path(sdk_path).resolve()
    if not sdk_path.is_file():
        raise FileNotFoundError(f"SDK CLI not found: {sdk_path}")
    source = sdk_path.parent.parent / "compiler" / "qecompile.py"
    spec = importlib.util.spec_from_file_location("qeaf_compiler", source)
    compiler = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(compiler)
    return compiler, sdk_path


def main():
    parser = argparse.ArgumentParser(description="Build this QEAPP project using the shared SDK compiler")
    parser.add_argument("--config", choices=("release", "debug", "all"), default="all")
    parser.add_argument("--project", type=Path, default=PROJECT)
    parser.add_argument("--sdk", type=Path, help="Path to SDK cli/qeaf.py (or set VQEAF_SDK)")
    args = parser.parse_args()
    try:
        compiler, sdk_path = load_compiler(args.sdk)
        return compiler.main(["--project", str(args.project), "--config", args.config,
                              "--sdk", str(sdk_path)])
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
