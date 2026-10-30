FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ARG DEBIAN_FRONTEND=noninteractive

# Toolchain for bare-metal x86 bootloader development:
#   assemblers/compilers, emulator (+OVMF UEFI firmware), debugger, and disk/ISO image tools.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-multilib \
        binutils \
        nasm \
        make \
        cmake \
        clang \
        lld \
        gdb \
        qemu-system-x86 \
        qemu-utils \
        ovmf \
        xorriso \
        mtools \
        dosfstools \
        grub-pc-bin \
        grub-common \
        hexedit \
        xxd \
        python3-pil \
        novnc \
        wget \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# GitHub CLI (auth lives in the shadebootloader-gh volume, see devcontainer.json).
RUN mkdir -p -m 755 /etc/apt/keyrings \
    && wget -qO /etc/apt/keyrings/githubcli-archive-keyring.gpg https://cli.github.com/packages/githubcli-archive-keyring.gpg \
    && chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
        > /etc/apt/sources.list.d/github-cli.list \
    && apt-get update && apt-get install -y gh \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Let gdb load project-local .gdbinit files (e.g. "target remote :1234").
RUN echo "set auto-load safe-path /" > /home/vscode/.gdbinit \
    && chown vscode:vscode /home/vscode/.gdbinit

# Claude Code CLI (native installer, per-user; settings live in the shadebootloader-claude volume).
USER vscode
RUN curl -fsSL https://claude.ai/install.sh | bash \
    && echo 'export PATH="$HOME/.local/bin:$PATH"' >> /home/vscode/.bashrc
ENV PATH="/home/vscode/.local/bin:${PATH}"
USER root
