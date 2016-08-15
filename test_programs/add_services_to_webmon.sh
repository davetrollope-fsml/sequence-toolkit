
num=20
if [ "$1" != "" ]; then
   num=$1
fi

# Start 20 clients with different names
cd ../examples
x=0
while [ $x -lt $num ]
do
	./simple_client -S "Client Service $x" -s 10000 &
	x=$(($x + 1))
done

