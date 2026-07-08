mkdir lab1
cd lab1/

tar -xf /work/rcssserver-15.2.2.tar
mv rcssserver-15.2.2/ team1/
cp /work/Lab1/client.cpp team1/src/
cp /work/Lab1/run.sh team1/
cd team1/
CXXFLAGS="-std=gnu++98" CPPFLAGS="-I/usr/include" LDFLAGS="-L/usr/lib/aarch64-linux-gnu" ./configure --prefix=/usr/local --with-boost=/usr --with-boost-libdir=/usr/lib/aarch64-linux-gnu --with-boost-system=boost_system --with-boost-filesystem=boost_filesystem
make -j1
make install

chmod u+x run.sh
./run.sh
