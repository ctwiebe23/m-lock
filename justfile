build:
	[ -d bin ] || mkdir bin
	gcc -Wall test/main.c -o run_test
	# gcc -Wall src/mlock.c -c -o bin/mlock.o
	# gcc -Wall test/main.c bin/mlock.o -o run_test

cmp LOOPS: build
	./run_test {{LOOPS}}
	./run_test {{LOOPS}} --malloc

clean:
	[ ! -d bin ] || rm -r bin
	[ ! -f run_test ] || rm run_test
