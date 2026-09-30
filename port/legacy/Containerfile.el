ARG IMAGE
FROM ${IMAGE}
ARG TARGET
RUN dnf install -y dnf-plugins-core epel-release && \
    if dnf repolist --all | awk '{print $1}' | grep -qx crb; then dnf config-manager --set-enabled crb; fi && \
    if dnf repolist --all | awk '{print $1}' | grep -qx powertools; then dnf config-manager --set-enabled powertools; fi && \
    dnf install -y gcc libX11-devel python3 shadow-utils util-linux dejavu-sans-fonts \
      /usr/bin/dbus-run-session /usr/bin/xauth && \
    if [ "$TARGET" = el10 ]; then \
      dnf install -y xwayland-run mutter xorg-x11-server-Xwayland; \
    else dnf install -y xorg-x11-server-Xvfb; fi && \
    useradd -u 10001 -m smoke
COPY port/ci/window-probe.c /tmp/window-probe.c
RUN gcc -std=c11 -O2 -Wall -Wextra -Werror /tmp/window-probe.c -lX11 -o /usr/local/bin/window-probe
COPY port/legacy/capture-window.c /tmp/capture-window.c
RUN gcc -std=c11 -O2 -Wall -Wextra -Werror /tmp/capture-window.c -lX11 -o /usr/local/bin/capture-window
COPY port/legacy/cpu-id.c /tmp/cpu-id.c
RUN gcc -std=c11 -O2 -Wall -Wextra -Werror /tmp/cpu-id.c -o /usr/local/bin/cpu-id
COPY port/legacy/smoke.py /opt/legacy-smoke.py
USER 10001
ENV HOME=/home/smoke XDG_RUNTIME_DIR=/tmp/legacy-xdg
ENTRYPOINT ["python3", "/opt/legacy-smoke.py"]
