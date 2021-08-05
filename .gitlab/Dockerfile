FROM fedora:latest

RUN dnf install --nogpg -y dnf-plugins-core git gnome-common gtk-doc meson \
 && dnf builddep --nogpg -y gnome-autoar \
 && dnf clean all
