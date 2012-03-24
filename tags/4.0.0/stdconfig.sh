#! /bin/sh
if test -f ./configure; then
    ./configure --prefix=/usr --sysconfdir=/etc --enable-silent-rules
else
    echo "------ERROR-------"
    echo "File 'configure' does not exist. You must create that using autotools first."
    echo "If you have autotools installed this could easily be done by running:"
    echo "$>autoreconf"
    echo "in this directory."
    exit 1
fi
