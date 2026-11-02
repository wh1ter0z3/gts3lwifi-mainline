# lk2nd runs in AArch32 on msm8996: a bare-metal ARM toolchain, dtc for its device
# trees. Nothing from the lk2nd tree runs on the host.
FROM docker.io/library/ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends \
	build-essential cpio device-tree-compiler gcc-arm-none-eabi libnewlib-arm-none-eabi python3 \
	&& rm -rf /var/lib/apt/lists/*
