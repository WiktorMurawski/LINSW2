OBJS := main.o 
worms: $(OBJS)
	$(CC) -o morse $(CFLAGS) $(LDFLAGS) $(OBJS) -l gpiod
$(OBJS) : %.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

