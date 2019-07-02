#!\bin\bash
TARGET := readline_firdin_demo
 
OBJS := main.c
OBJS += readline.c
 
 
$(TARGET):$(OBJS)
	echo $(OBJS)
	$(CC) -Wall -W -g -o $(TARGET) $(OBJS)
 
 
clean:
	rm $(TARGET) -rf
