copyfile: binn.c copy_th.c
	gcc $^ -lpthread -o $@
