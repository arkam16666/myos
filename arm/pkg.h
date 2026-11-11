// pkg.h - the package manager
#ifndef PKG_H
#define PKG_H

void pkg_command(const char *args);     // "list", "update", "search", "install NAME", ...
void apt_command(const char *args);     // the same with apt's words: "install -y NAME", "show NAME" ...
int pkg_python_app(const char *name);   // 1 if NAME is an installed Python app (NAME.py)

#endif
