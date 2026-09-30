#!/bin/sh
# Legacy DKMS sign_tool interface: <kernel-release> <module-file>.
# Modern DKMS: prefer its built-in mok_signing_key/mok_certificate support.
set -eu
[ "$#" -eq 2 ] || { echo "usage: $0 kernel-release module.ko" >&2; exit 2; }
kernel=$1
module=$2
key=${OCTOOL_MOK_KEY:-/var/lib/dkms/octool-mok.key}
cert=${OCTOOL_MOK_CERT:-/var/lib/dkms/octool-mok.der}
signer=/lib/modules/$kernel/build/scripts/sign-file
[ -x "$signer" ] || { echo "missing kernel sign-file: $signer" >&2; exit 1; }
[ -r "$key" ] && [ -r "$cert" ] || { echo "missing MOK key/certificate: $key / $cert" >&2; exit 1; }
"$signer" sha256 "$key" "$cert" "$module"
[ -n "$(modinfo -F signer "$module")" ] || { echo 'module signature absent after signing' >&2; exit 1; }
