OBJS := morse.o 
morse: $(OBJS)
	$(CC) -o morse $(CFLAGS) $(LDFLAGS) $(OBJS) -l gpiod
$(OBJS) : %.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

