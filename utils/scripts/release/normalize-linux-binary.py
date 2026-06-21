#!/usr/bin/env python3
import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: normalize-linux-binary.py <binary> <openssl-built-on-value>", file=sys.stderr)
        return 2

    binary_path = Path(sys.argv[1])
    replacement = sys.argv[2].encode("ascii")
    pattern = re.compile(
        rb"built on: [A-Z][a-z]{2} [A-Z][a-z]{2} [ 0-9][0-9] "
        rb"[0-9]{2}:[0-9]{2}:[0-9]{2} [0-9]{4} UTC"
    )

    data = binary_path.read_bytes()
    match = pattern.search(data)
    if not match:
        return 0

    if len(replacement) != len(match.group(0)):
        print(
            f"Replacement length {len(replacement)} does not match OpenSSL build string length {len(match.group(0))}.",
            file=sys.stderr,
        )
        return 1

    normalized = pattern.sub(replacement, data)
    if normalized != data:
        binary_path.write_bytes(normalized)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
