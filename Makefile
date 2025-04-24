sshell: sshell.o
	gcc -g -Wall -Wextra -Werror -o sshell sshell.o
sshell.o: sshell.c
	gcc -g -Wall -Wextra -Werror -c sshell.c
clean:
	rm -f sshell sshell.o
run: sshell
	./sshell