CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

protocol_tests: protocol_tests.c ../protocol/syna_tcm.c ../protocol/syna_hid.c
	$(CC) $(CFLAGS) -I../protocol $^ -o $@

test: protocol_tests
	./protocol_tests

clean:
	rm -f protocol_tests
