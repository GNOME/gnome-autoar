FROM fedora:rawhide

RUN dnf install --nogpg -y dnf-plugins-core git gnome-common gtk-doc \
 && dnf builddep --nogpg -y gnome-autoar \
 && dnf clean all
