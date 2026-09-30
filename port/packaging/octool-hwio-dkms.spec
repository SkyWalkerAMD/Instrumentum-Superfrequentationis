Name:           octool-hwio-dkms
Version:        @VERSION@
Release:        1%{?dist}
Summary:        OCTool portable hardware access module sources for DKMS
License:        GPL-2.0-only
Source0:        octool-hwio-%{version}-src.tar.gz
BuildArch:      noarch
ExclusiveArch:  x86_64 noarch
Requires:       dkms >= 2.8, gcc, make, kernel-devel, elfutils-libelf-devel, kmod
Requires(post): dkms, procps-ng
Requires(preun): dkms, procps-ng
Recommends:     mokutil, openssl

%description
Sources for a DKMS module preserving OCTool's 96-byte /dev/mydev MMIO protocol.
Secure Boot requires a locally trusted module signing certificate.

%prep
%setup -q -n octool-hwio-%{version}

%build

%install
mkdir -p %{buildroot}
cp -a usr %{buildroot}/

%post
set -eu
# Keep add/remove directly in RPM scriptlets: DKMS associates its upgrade lock
# with the RPM transaction's parent PID. A helper around add breaks that link.
if dkms add -m octool-hwio -v %{version} --rpm_safe_upgrade; then
    :
else
    rc=$?
    # Exit 3 is only acceptable for a registered same-version reinstall.
    status=$(dkms status -m octool-hwio -v %{version})
    if [ "$rc" -ne 3 ] || [ -z "$status" ]; then exit "$rc"; fi
fi
exec /usr/libexec/octool/dkms-register.sh %{version}

%preun
set -eu
status=$(dkms status -m octool-hwio -v %{version})
if [ -n "$status" ]; then
    dkms remove -m octool-hwio -v %{version} --all --rpm_safe_upgrade
fi

%files
/usr/src/octool-hwio-%{version}
/usr/libexec/octool
/usr/lib/udev/rules.d/99-octool-hwio.rules
/usr/lib/modules-load.d/octool-msr.conf
