#!/usr/bin/env bash
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
rm -rf cuda-keyring_1.1-1_all.deb
sudo apt-get update
sudo apt-get -y install cuda

ZSH_INIT="$HOME/.zshrc"
if [ -f "$ZSH_INIT" ] && ! grep -q "cuda" "$ZSH_INIT"; then
    echo 'export PATH="/usr/local/cuda/bin:$PATH"' >> "$ZSH_INIT"
    echo "Added CUDA path to $ZSH_INIT"
fi

BASH_INIT="$HOME/.bashrc"
if [ -f "$BASH_INIT" ] && ! grep -q "cuda" "$BASH_INIT"; then
    echo 'export PATH="/usr/local/cuda/bin:$PATH"' >> "$BASH_INIT"
    echo "Added CUDA path to $BASH_INIT"
fi

FISH_INIT="$HOME/.config/fish/config.fish"
if [ -f "$FISH_INIT" ] && ! grep -q "cuda" "$FISH_INIT"; then
    echo "fish_add_path /usr/local/cuda/bin" >> "$FISH_INIT"
    echo "Added CUDA path to $FISH_INIT"
fi
