CC=gcc
COPTS = -g -c -std=gnu99 -Wall -pedantic
LOPTS = -g

all : mftpserve mftp 

mftpserve: mftpserve.o mftp.h
	$(CC) $(LOPTS) -o mftpserve mftpserve.o

mftpserve.o:	mftpserve.c
	$(CC) $(COPTS) mftpserve.c 
  
mftp: mftp.o mftp.h 
	$(CC) $(LOPTS) -o mftp mftp.o

mftp.o:	mftp.c
	$(CC) $(COPTS) mftp.c

clean:
	rm mftp.o mftpserve.o mftp mftpserve
