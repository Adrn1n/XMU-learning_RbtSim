FROM ubuntu:18.04 AS base
RUN sed -i -e 's|http://ports.ubuntu.com/ubuntu-ports|http://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports|g' -e 's|http://ports.ubuntu.com|https://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports|g' /etc/apt/sources.list
RUN apt-get update && apt-get install -y build-essential gcc g++ make cmake autoconf automake libtool pkg-config flex bison tar gzip xz-utils wget curl git gdb net-tools iputils-ping && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace

FROM base AS server
RUN apt-get update && apt-get install -y libboost-all-dev zlib1g-dev libbz2-dev libexpat1-dev libxml2-dev libssl-dev m4 && rm -rf /var/lib/apt/lists/*
WORKDIR /tmp
RUN wget -q http://ftp.gnu.org/gnu/bison/bison-2.7.1.tar.gz && tar -xf bison-2.7.1.tar.gz && cd bison-2.7.1 && ./configure --prefix=/usr/local && make -j"$(nproc)" && make install
ENV PATH=/usr/local/bin:$PATH
WORKDIR /build
COPY rcssserver-15.2.2.tar /build/
RUN tar -xf rcssserver-15.2.2.tar && cd rcssserver-15.2.2 && CXXFLAGS="-std=gnu++98" CPPFLAGS="-I/usr/include" LDFLAGS="-L/usr/lib/aarch64-linux-gnu" ./configure --prefix=/usr/local --with-boost=/usr --with-boost-libdir=/usr/lib/aarch64-linux-gnu --with-boost-system=boost_system --with-boost-filesystem=boost_filesystem && make -j1 && make install
RUN printf "/usr/local/lib\n" > /etc/ld.so.conf.d/local.conf && ldconfig
CMD ["rcssserver"]

FROM base AS monitor
RUN apt-get update && apt-get install -y libboost-all-dev zlib1g-dev qt4-qmake libqt4-dev libx11-dev libxext-dev libxi-dev libxt-dev libxrender-dev libfontconfig1-dev libfreetype6-dev libgl1-mesa-dev libaudio-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /build
COPY rcssmonitor-15.1.1.tar /build/
RUN tar -xf rcssmonitor-15.1.1.tar && cd rcssmonitor-15.1.1 && CXXFLAGS="-std=gnu++98" CPPFLAGS="-I/usr/include" LDFLAGS="-L/usr/lib/aarch64-linux-gnu" ./configure --prefix=/usr/local --with-boost=/usr --with-boost-libdir=/usr/lib/aarch64-linux-gnu --with-boost-system=boost_system && sed -i 's/-pthread-l/-pthread -l/g' Makefile src/Makefile && make -j"$(nproc)" && make install
CMD ["rcssmonitor", "--server", "server"]

FROM base AS agent
RUN apt-get update && apt-get install -y libboost-dev libboost-system-dev libboost-filesystem-dev zlib1g-dev tcsh && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace/uva_base_v13.2
CMD ["/bin/bash"]
