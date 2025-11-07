build:
	[ -d bin ] || mkdir bin
	gcc -Wall src/mlock.c -c -o bin/mlock.o
	gcc -Wall test/main.c bin/mlock.o -o run_test

cmp LOOPS: build
	./run_test {{LOOPS}} --parallel

clean:
	[ ! -d bin ] || rm -r bin
	[ ! -d doc ] || rm -r doc
	[ ! -f run_test ] || rm run_test

doc:
	[ -d doc ] || mkdir doc
	[ -d doc/changelog ] || mkdir doc/changelog
	pandoc README.md -so doc/index.html -d pandoc.yml -d readme
	pandoc CHANGELOG.md -so doc/changelog/index.html -d pandoc.yml -d readme
