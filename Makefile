all:
	gcc -g -o myChannels myChannels.c -pthread
clean:
	$(RM) myChannels