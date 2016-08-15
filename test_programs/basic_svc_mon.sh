
pkill simple
pkill stkhttpd

if [ ! -r daemons ]; then
	cd ..
fi
cd daemons
./stkhttpd -p 6001 & sleep 1
cd ../examples
./simple_server & sleep 1
./simple_client &

