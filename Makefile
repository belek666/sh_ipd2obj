
CC      =  gcc.exe
OBJ      = main.o tim.o
BIN      = ipd2obj.exe

all: $(BIN)

clean:
	rm -f $(OBJ) $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $(BIN)

%.o: %.c
	$(CC) -c $*.c -o $@
