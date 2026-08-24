#!/usr/bin/bash

curl https://gitlab.freedesktop.org/hadess/check-abi/-/raw/main/contrib/check-abi-fedora.sh | bash

dnf clean all

rm -r check-abi
