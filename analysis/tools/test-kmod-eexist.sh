#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
module_path=${1:?usage: test-kmod-eexist.sh MODULE.ko [OUTPUT.json]}
output_path=${2:-"$repo_root/build/kmod-eexist/result.json"}
mkdir -p "$(dirname "$output_path")"

if [[ $(id -u) -ne 0 ]]; then
	echo "must run as root to load/unload the test module" >&2
	exit 2
fi
[[ -r "$module_path" ]] || { echo "module is not readable: $module_path" >&2; exit 2; }

kernel_release=$(uname -r)
module_name=$(modinfo -F name "$module_path")
[[ "$module_name" == octool_hwio ]] || {
	echo "unexpected module name: $module_name" >&2
	exit 2
}
[[ ! -e "/sys/module/$module_name" ]] || {
	echo "$module_name is already loaded; refusing to disturb the runner" >&2
	exit 2
}

module_sha=$(sha256sum "$module_path" | awk '{print $1}')
module_vermagic=$(modinfo -F vermagic "$module_path")
module_signer=$(modinfo -F signer "$module_path")
module_sig_hashalgo=$(modinfo -F sig_hashalgo "$module_path")
probe="$repo_root/build/kmod-eexist-probe"
mkdir -p "$(dirname "$probe")"
cc -std=c11 -O2 -Wall -Wextra -Werror \
	-o "$probe" "$repo_root/analysis/tools/kmod-eexist-probe.c"

loaded=0
cleanup() {
	if [[ $loaded -eq 1 ]]; then
		rmmod "$module_name" || true
	fi
}
trap cleanup EXIT

insmod "$module_path"
loaded=1

probe_rc=0
probe_output=$("$probe" "$module_path") || probe_rc=$?
if command -v udevadm >/dev/null 2>&1; then
	udevadm settle --timeout=5 || true
fi
class_device="/sys/class/$module_name/mydev"
devnode="/dev/mydev"
class_present=false
devnode_present=false
[[ -e "$class_device" ]] && class_present=true
[[ -c "$devnode" ]] && devnode_present=true
[[ "$class_present" == true ]] || probe_rc=1

python3 - "$output_path" "$kernel_release" "$module_name" "$module_sha" \
	"$module_vermagic" "$module_signer" "$module_sig_hashalgo" \
	"$probe_rc" "$class_present" "$devnode_present" \
	"$probe_output" <<'PY'
import json
import pathlib
import sys

(out, kernel, module, digest, vermagic, signer, sig_hashalgo, rc,
 class_present, devnode_present, raw) = sys.argv[1:]
try:
    result = json.loads(raw)
except Exception:
    result = {"raw_probe_output": raw}
report = {
    "kernel_release": kernel,
    "module_name": module,
    "module_sha256": digest,
    "module_vermagic": vermagic,
    "module_signer": signer,
    "module_signature_hash": sig_hashalgo,
    "initial_load": "insmod (finit_module)",
    "probe_exit_code": int(rc),
    "module_class_device_present_after_probe": class_present == "true",
    "dev_mydev_present_after_probe": devnode_present == "true",
    "hardware_io_attempted": False,
    "probe": result,
}
pathlib.Path(out).write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
PY

if [[ $probe_rc -ne 0 ]]; then
	echo "duplicate init_module did not return EEXIST; see $output_path" >&2
	exit 1
fi
