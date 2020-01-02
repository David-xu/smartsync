APP:=smartsync
SRC_DIR:= ./src
SRC:= $(foreach n, $(SRC_DIR), $(wildcard $(n)/*.c))
OBJ:= $(SRC:.c=.o)
CFLAGS:=-Wall -Wno-unused-function -O2 #-g

all: $(APP)

$(OBJ):$(SRC)

$(APP): $(OBJ)
	gcc -o $@ $^ $(CFLAGS) -lpthread

clean:
	rm -rf $(APP) $(OBJ)

PHONY = clean
.PHONY:$(PHONY)
