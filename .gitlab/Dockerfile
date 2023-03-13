FROM fedora:rawhide

RUN dnf install --nogpg -y dnf-plugins-core git gnome-common gtk-doc meson \
 && dnf builddep --nogpg -y gnome-autoar \
 && dnf clean all

RUN curl https://gitlab.freedesktop.org/hadess/check-abi/-/raw/main/contrib/check-abi-fedora.sh | bash \
 && dnf clean all \
 && rm -r check-abi
